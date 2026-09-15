#include "DualityEditor/AssetInspectors.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "DualityEditor/AssetInspectorRegistry.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AudioImportSettings.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/PhysicsMaterial.h"
#include "DualityEngine/Asset/PhysicsMaterialLoader.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

using json = nlohmann::json;

namespace Duality {

    namespace {

        constexpr ImVec4 CodePlain{ 0.82f, 0.84f, 0.90f, 1.0f };
        constexpr ImVec4 CodeKeyword{ 0.95f, 0.48f, 0.76f, 1.0f };
        constexpr ImVec4 CodeType{ 0.35f, 0.78f, 0.96f, 1.0f };
        constexpr ImVec4 CodeString{ 0.82f, 0.73f, 0.42f, 1.0f };
        constexpr ImVec4 CodeNumber{ 0.72f, 0.82f, 0.48f, 1.0f };
        constexpr ImVec4 CodeComment{ 0.42f, 0.58f, 0.47f, 1.0f };
        constexpr ImVec4 CodePreprocessor{ 0.78f, 0.58f, 0.96f, 1.0f };

        struct SourcePreviewCache {
            std::string Path;
            std::filesystem::file_time_type LastWriteTime{};
            std::string Content;
            std::string Error;
        };

        SourcePreviewCache& GetSourcePreview(const std::string& path) {
            static SourcePreviewCache cache;
            std::error_code error;
            const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(path, error);
            if (cache.Path == path && !error && cache.LastWriteTime == lastWriteTime)
                return cache;

            cache = {};
            cache.Path = path;
            cache.LastWriteTime = lastWriteTime;
            const uintmax_t size = std::filesystem::file_size(path, error);
            constexpr uintmax_t MaxPreviewBytes = 512u * 1024u;
            if (error) {
                cache.Error = "Could not read file metadata.";
                return cache;
            }
            if (size > MaxPreviewBytes) {
                cache.Error = "Preview is limited to 512 KB; open this file in your external editor.";
                return cache;
            }
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                cache.Error = "Could not open file.";
                return cache;
            }
            cache.Content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return cache;
        }

        bool IsCodeKeyword(std::string_view word) {
            static constexpr std::string_view keywords[] = {
                "alignas", "alignof", "auto", "break", "case", "catch", "class", "const", "constexpr", "continue",
                "default", "delete", "do", "else", "enum", "explicit", "export", "extern", "for", "friend", "if",
                "inline", "namespace", "new", "noexcept", "operator", "private", "protected", "public", "return", "sizeof",
                "static", "struct", "switch", "template", "this", "throw", "try", "typedef", "typename", "union", "using",
                "virtual", "volatile", "while", "override", "final", "true", "false", "nullptr"
            };
            return std::find(std::begin(keywords), std::end(keywords), word) != std::end(keywords);
        }

        bool IsCodeType(std::string_view word) {
            static constexpr std::string_view types[] = {
                "bool", "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void", "wchar_t",
                "size_t", "string", "std", "glm", "Entity", "Behaviour", "AssetRef", "ScriptableObject", "vector"
            };
            return std::find(std::begin(types), std::end(types), word) != std::end(types);
        }

        void DrawCodeSegment(const char* begin, size_t length, const ImVec4& color, bool& wroteToken) {
            if (length == 0)
                return;
            if (wroteToken)
                ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(color, "%.*s", static_cast<int>(length), begin);
            wroteToken = true;
        }

        // Small syntax highlighter for the Inspector preview. This intentionally stays a
        // tokenizer rather than bringing a full editor widget into the 3DS-focused codebase:
        // previews are read-only, while editing belongs in the configured external IDE.
        void DrawCodeLine(const std::string& line, int lineNumber, bool& inBlockComment) {
            ImGui::TextDisabled("%4d", lineNumber);
            ImGui::SameLine(0.0f, 8.0f);
            bool wroteToken = false;
            size_t index = 0;
            const size_t first = line.find_first_not_of(" \t");
            if (!inBlockComment && first != std::string::npos && line[first] == '#') {
                DrawCodeSegment(line.data(), line.size(), CodePreprocessor, wroteToken);
                return;
            }

            while (index < line.size()) {
                const char current = line[index];
                if (inBlockComment) {
                    const size_t close = line.find("*/", index);
                    const size_t end = close == std::string::npos ? line.size() : close + 2;
                    DrawCodeSegment(line.data() + index, end - index, CodeComment, wroteToken);
                    inBlockComment = close == std::string::npos;
                    index = end;
                } else if (current == '/' && index + 1 < line.size() && line[index + 1] == '/') {
                    DrawCodeSegment(line.data() + index, line.size() - index, CodeComment, wroteToken);
                    break;
                } else if (current == '/' && index + 1 < line.size() && line[index + 1] == '*') {
                    inBlockComment = true;
                } else if (current == '"' || current == '\'') {
                    const char quote = current;
                    size_t end = index + 1;
                    while (end < line.size()) {
                        if (line[end] == '\\') { end += 2; continue; }
                        if (line[end++] == quote) break;
                    }
                    DrawCodeSegment(line.data() + index, std::min(end, line.size()) - index, CodeString, wroteToken);
                    index = std::min(end, line.size());
                } else if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
                    size_t end = index + 1;
                    while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) ++end;
                    const std::string_view word(line.data() + index, end - index);
                    DrawCodeSegment(line.data() + index, end - index,
                        IsCodeKeyword(word) ? CodeKeyword : (IsCodeType(word) ? CodeType : CodePlain), wroteToken);
                    index = end;
                } else if (std::isdigit(static_cast<unsigned char>(current))) {
                    size_t end = index + 1;
                    while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '.' || line[end] == '_')) ++end;
                    DrawCodeSegment(line.data() + index, end - index, CodeNumber, wroteToken);
                    index = end;
                } else {
                    DrawCodeSegment(line.data() + index, 1, CodePlain, wroteToken);
                    ++index;
                }
            }
            if (!wroteToken)
                ImGui::TextUnformatted(" ");
        }

        void DrawSourcePreview(const std::string& path, EditorContext&) {
            SourcePreviewCache& preview = GetSourcePreview(path);
            if (!preview.Error.empty()) {
                ImGui::TextDisabled("%s", preview.Error.c_str());
                return;
            }

            const int lineCount = static_cast<int>(std::count(preview.Content.begin(), preview.Content.end(), '\n')) + 1;
            ImGui::TextDisabled("Read-only preview | %d lines | %zu bytes", lineCount, preview.Content.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy All"))
                ImGui::SetClipboardText(preview.Content.c_str());
            ImGui::Separator();

            ImGui::BeginChild("##SourcePreview", ImVec2(0.0f, 360.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            bool inBlockComment = false;
            size_t lineStart = 0;
            int lineNumber = 1;
            while (lineStart <= preview.Content.size()) {
                const size_t lineEnd = preview.Content.find('\n', lineStart);
                const size_t length = (lineEnd == std::string::npos ? preview.Content.size() : lineEnd) - lineStart;
                std::string line = preview.Content.substr(lineStart, length);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                DrawCodeLine(line, lineNumber++, inBlockComment);
                if (lineEnd == std::string::npos)
                    break;
                lineStart = lineEnd + 1;
            }
            ImGui::EndChild();
        }

        void DrawMaterialAsset(const std::string& path, EditorContext&) {
            Material material = MaterialLoader::Load(path);
            ImGui::TextUnformatted("3D Material");
            ImGui::TextDisabled("Unlit preserves authored color. VertexLit uses the scene's main Directional Light.");
            ImGui::TextDisabled("Imported OBJ vertex colors multiply the material for baked AO/light; meshes without them stay white.");
            ImGui::Separator();
            bool changed = false;
            for (auto& field : Material::Fields())
                changed |= DrawFieldWidget(field, &material);
            if (changed)
                MaterialLoader::Save(path, material);
        }

        void DrawPhysicsMaterialAsset(const std::string& path, EditorContext&) {
            PhysicsMaterial material = PhysicsMaterialLoader::Load(path);
            bool changed = false;
            for (auto& field : PhysicsMaterial::Fields())
                changed |= DrawFieldWidget(field, &material);
            if (changed)
                PhysicsMaterialLoader::Save(path, material);
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

        void DrawPrefabAsset(const std::string& path, EditorContext& ctx) {
            if (ImGui::Button("Open Prefab"))
                ctx.RequestOpenPrefabPath = path;
            ImGui::SameLine();
            ImGui::TextDisabled("Edit in isolated Prefab Mode");
            ImGui::Separator();
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
            if (ImGui::Button("Open Scene") && !ctx.IsEditingPrefab)
                OpenScene(ctx, path);
            if (ctx.IsEditingPrefab)
                ImGui::TextDisabled("Exit Prefab Mode to open a scene.");
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
                ctx.Renderer.UnloadAllFonts();
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
        AssetInspectorRegistry::Register({ ".physmat", "Physics Material", &DrawPhysicsMaterialAsset });
        AssetInspectorRegistry::Register({ ".prefab", "Prefab", &DrawPrefabAsset });
        AssetInspectorRegistry::Register({ ".asset", "ScriptableObject", &DrawScriptableObjectAsset });
        AssetInspectorRegistry::Register({ ".scene", "Scene", &DrawSceneAsset });
        AssetInspectorRegistry::Register({ ".wav", "Audio Clip", &DrawAudioAsset });

        for (const char* ext : { ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".glsl", ".pica", ".cmake" })
            AssetInspectorRegistry::Register({ ext, "Code", &DrawSourcePreview });
        for (const char* ext : { ".json", ".txt", ".md", ".ini", ".cfg" })
            AssetInspectorRegistry::Register({ ext, "Text", &DrawSourcePreview });

        // Same recognized-extension list as ThumbnailCache::IsImageFile -- kept as its own
        // literal list here rather than a shared constant, one line of duplication, matching
        // this codebase's own established "not worth a shared utility for one line" convention
        // (see ConsolePanel.cpp/HierarchyPanel.cpp's identical ContainsCaseInsensitive helpers).
        for (const char* ext : { ".png", ".jpg", ".jpeg", ".bmp", ".tga" })
            AssetInspectorRegistry::Register({ ext, "Texture", &DrawTextureAsset });
    }

}
