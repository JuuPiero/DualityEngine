#include "DualityEditor/AssetInspectors.h"

#include <fstream>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "DualityEditor/AssetInspectorRegistry.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AudioImportSettings.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

using json = nlohmann::json;

namespace Duality {

    namespace {

        void DrawMaterialAsset(const std::string& path, EditorContext&) {
            Material material = MaterialLoader::Load(path);
            bool changed = false;
            for (auto& field : Material::Fields())
                changed |= DrawFieldWidget(field, &material);
            if (changed)
                MaterialLoader::Save(path, material);
        }

        void DrawScriptableObjectAsset(const std::string& path, EditorContext&) {
            ScriptableObjectLoader::Loaded loaded = ScriptableObjectLoader::Load(path);
            if (!loaded.Instance) {
                ImGui::TextDisabled("<failed to load '%s' -- is GameScripts loaded?>", path.c_str());
                return;
            }

            const ScriptableObjectFactoryEntry* type = ScriptableObjectRegistry::Find(loaded.ClassName);
            ImGui::Text("Class: %s", loaded.ClassName.c_str());
            ImGui::Separator();
            for (auto& field : type->Fields) {
                if (DrawFieldWidget(field, loaded.Instance))
                    ScriptableObjectLoader::Save(path, loaded.ClassName, loaded.Instance);
            }
        }

        // Shared by Prefab/Scene below -- both files share the exact same top-level
        // {"Entities": [{...,"Parent": N}, ...]} shape (see EntitySerialization.h).
        bool TryReadEntities(const std::string& path, json& outEntities) {
            std::ifstream file(path);
            if (!file) {
                ImGui::TextDisabled("<could not open '%s'>", path.c_str());
                return false;
            }
            json root;
            try {
                file >> root;
            } catch (const json::parse_error&) {
                ImGui::TextDisabled("<failed to parse '%s'>", path.c_str());
                return false;
            }
            if (!root.contains("Entities")) {
                ImGui::TextDisabled("<no Entities>");
                return false;
            }
            outEntities = root["Entities"];
            return true;
        }

        // Read-only summary -- editing a Prefab asset's fields in place (outside any live
        // Scene) would need either a hidden Scene to instantiate into or generalizing
        // EntitySerialization to work off raw JSON without an Entity at all; both are bigger
        // features than "show something useful when selected," left for later (see
        // ROADMAP.md).
        void DrawPrefabAsset(const std::string& path, EditorContext&) {
            json entities;
            if (!TryReadEntities(path, entities) || entities.empty())
                return;

            ImGui::Text("%d entit%s total", static_cast<int>(entities.size()), entities.size() == 1 ? "y" : "ies");

            // Index 0 is always the prefab's own root -- PrefabSerializer::Save's collect()
            // visits it first, before any descendant.
            const json& rootJson = entities[0];
            std::string rootName = rootJson.contains("Name") ? rootJson["Name"].value("Name", std::string()) : std::string();
            if (!rootName.empty())
                ImGui::Text("Root: %s", rootName.c_str());

            ImGui::Separator();
            ImGui::TextDisabled("Components on root:");
            for (auto& [key, value] : rootJson.items()) {
                if (key == "Parent")
                    continue;
                ImGui::BulletText("%s", key.c_str());
            }
        }

        // Also read-only (a Scene is edited by opening it, not in place here) -- lists every
        // root entity (Parent == -1) by name, which is the meaningful "what's in this scene at
        // a glance" summary, unlike Prefab's single well-defined root. The "Open Scene" button
        // is the same action as double-clicking this file in the Content Browser (SceneOps::
        // OpenScene) -- offered here too since a file already selected (this Inspector showing)
        // is one click away from opening, instead of needing to go find and double-click it again.
        void DrawSceneAsset(const std::string& path, EditorContext& ctx) {
            if (ImGui::Button("Open Scene"))
                OpenScene(ctx, path);
            ImGui::Separator();

            json entities;
            if (!TryReadEntities(path, entities))
                return;

            ImGui::Text("%d entit%s total", static_cast<int>(entities.size()), entities.size() == 1 ? "y" : "ies");
            ImGui::Separator();
            ImGui::TextDisabled("Root entities:");
            for (auto& entityJson : entities) {
                if (entityJson.value("Parent", -1) != -1)
                    continue;
                std::string name = entityJson.contains("Name") ? entityJson["Name"].value("Name", std::string()) : std::string();
                ImGui::BulletText("%s", name.empty() ? "<unnamed>" : name.c_str());
            }
        }

        void DrawTextureAsset(const std::string& path, EditorContext& ctx) {
            TextureImportSettings settings = TextureImportSettings::Load(path);
            bool changed = false;

            const char* filterItems[] = { "Point", "Bilinear" };
            int filterIndex = settings.FilterMode == TextureFilterMode::Point ? 0 : 1;
            if (ImGui::Combo("Filter Mode", &filterIndex, filterItems, 2)) {
                settings.FilterMode = filterIndex == 0 ? TextureFilterMode::Point : TextureFilterMode::Bilinear;
                changed = true;
            }

            const char* wrapItems[] = { "Clamp", "Repeat" };
            int wrapIndex = settings.WrapMode == TextureWrapMode::Clamp ? 0 : 1;
            if (ImGui::Combo("Wrap Mode", &wrapIndex, wrapItems, 2)) {
                settings.WrapMode = wrapIndex == 0 ? TextureWrapMode::Clamp : TextureWrapMode::Repeat;
                changed = true;
            }

            if (ImGui::Checkbox("Generate Mipmaps", &settings.GenerateMipmaps))
                changed = true;

            if (changed) {
                TextureImportSettings::Save(path, settings);
                // OpenGLRenderer2D caches a decoded GL texture by path and only ever applies
                // import settings at load time -- nuking the whole cache forces every sprite
                // using this (or any other) texture to reload next frame with the new settings,
                // giving immediate visual feedback while tweaking. Heavier than a per-path
                // unload, but this already-existing method (added for SceneManager's scene-
                // transition cache clear) covers it with no new renderer API needed.
                ctx.Renderer.UnloadAllTextures();
            }
        }

        void DrawAudioAsset(const std::string& path, EditorContext&) {
            AudioImportSettings settings = AudioImportSettings::Load(path);
            if (ImGui::SliderFloat("Volume", &settings.Volume, 0.0f, 1.0f))
                AudioImportSettings::Save(path, settings);
        }

    }

    void RegisterBuiltinAssetInspectors() {
        AssetInspectorRegistry::Register({ ".mat", "Material", &DrawMaterialAsset });
        AssetInspectorRegistry::Register({ ".prefab", "Prefab", &DrawPrefabAsset });
        AssetInspectorRegistry::Register({ ".asset", "ScriptableObject", &DrawScriptableObjectAsset });
        AssetInspectorRegistry::Register({ ".scene", "Scene", &DrawSceneAsset });
        AssetInspectorRegistry::Register({ ".wav", "Audio Clip", &DrawAudioAsset });

        // Same recognized-extension list as ThumbnailCache::IsImageFile -- kept as its own
        // literal list here rather than a shared constant, one line of duplication, matching
        // this codebase's own established "not worth a shared utility for one line" convention
        // (see ConsolePanel.cpp/HierarchyPanel.cpp's identical ContainsCaseInsensitive helpers).
        for (const char* ext : { ".png", ".jpg", ".jpeg", ".bmp", ".tga" })
            AssetInspectorRegistry::Register({ ext, "Texture", &DrawTextureAsset });
    }

}
