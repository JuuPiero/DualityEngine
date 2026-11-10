#include "DualityEditor/Panels/PropertiesPanel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <set>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>

#include "DualityEditor/AssetInspectorRegistry.h"
#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorSettings.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/MultiEdit.h"
#include "DualityEditor/Panels/ContentBrowserPanel.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/ModelAsset.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    namespace {
        bool IsLive(EditorContext& ctx, Entity entity) {
            return entity && entity.GetScene() == &ctx.SceneRef && ctx.SceneRef.Registry().valid(entity.Handle());
        }

        std::string LowercaseExtension(const std::string& path) {
            std::string ext = std::filesystem::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

        bool ContainsCaseInsensitive(const std::string& haystack, const char* needle) {
            if (!needle || needle[0] == '\0')
                return true;
            const std::string query(needle);
            auto it = std::search(haystack.begin(), haystack.end(), query.begin(), query.end(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            return it != haystack.end();
        }

        const char* ComponentIcon(const std::string& name) {
            if (name == "Transform") return EditorIcons::Move;
            if (name == "Camera") return EditorIcons::Camera;
            if (name.find("Light") != std::string::npos) return EditorIcons::Light;
            if (name.find("Audio") != std::string::npos) return EditorIcons::Audio;
            if (name.find("Collider") != std::string::npos || name.find("Rigidbody") != std::string::npos || name.find("Raycaster") != std::string::npos) return EditorIcons::Bolt;
            if (name.find("Sprite") != std::string::npos || name == "UI Image") return EditorIcons::Image;
            if (name.find("Mesh") != std::string::npos) return EditorIcons::Cube;
            if (name.find("UI Text") != std::string::npos) return EditorIcons::Font;
            if (name.rfind("UI", 0) == 0 || name == "Canvas") return EditorIcons::Square;
            if (name.find("Animation") != std::string::npos || name.find("Animator") != std::string::npos) return EditorIcons::Play;
            if (name == "Tilemap") return EditorIcons::Grid;
            return EditorIcons::Settings;
        }

        FieldHandle* FindEnabledField(ComponentTypeInfo& type) {
            for (auto& field : type.Fields) {
                if (field.Name == "Enabled")
                    return &field;
            }
            return nullptr;
        }

        bool JsonNearlyEqual(const nlohmann::json& a, const nlohmann::json& b) { return MultiEdit::NearlyEqual(a, b); }

        Entity PrefabRootOf(Entity entity) {
            for (Entity current = entity; current; current = current.GetComponent<HierarchyComponent>().Parent) {
                if (current.HasComponent<PrefabInstanceComponent>())
                    return current;
            }
            return {};
        }

        // Pre-order like PrefabSerializer::Save, so index i matches the prefab file's entity i.
        void CollectPreOrder(Entity entity, std::vector<Entity>& out) {
            out.push_back(entity);
            for (Entity child : entity.GetComponent<HierarchyComponent>().Children)
                CollectPreOrder(child, out);
        }

        // Fields Unity never reports as prefab overrides on the instance root.
        bool IsRootOnlyField(const std::string& component, const std::string& field) {
            return (component == "Transform" && (field == "Translation" || field == "Rotation")) || component == "Name";
        }

        void OpenInShell(const std::string& path) {
            const std::string editor = EditorSettings::Get().ExternalEditorPath;
            if (!editor.empty())
                ShellExecuteA(nullptr, "open", editor.c_str(), ("\"" + path + "\"").c_str(), nullptr, SW_SHOWNORMAL);
            else
                ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        // Finds a script's source file (TypeScript <Class>.ts or C++ <Class>.h) in the project
        // scripts folder, then in the engine's shared GameScripts folder.
        std::string FindScriptSource(EditorContext& ctx, const std::string& className, bool typeScript) {
            std::vector<std::filesystem::path> roots;
            if (auto project = Project::GetActive())
                roots.push_back(project->GetAssetsDirectory());
            roots.push_back(std::filesystem::path(ctx.RepoRoot) / "GameScripts");
            const std::string fileName = className + (typeScript ? ".ts" : ".h");
            for (const auto& root : roots) {
                std::error_code error;
                for (auto it = std::filesystem::recursive_directory_iterator(root, error);
                     !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                    if (it->is_regular_file() && it->path().filename() == fileName)
                        return it->path().string();
                }
            }
            return {};
        }

        void DrawSelectedAsset(EditorContext& ctx, const std::string& path) {
            const AssetInspectorEntry* entry = AssetInspectorRegistry::FindByExtension(LowercaseExtension(path));
            ImGui::PushFont(EditorBoldFont());
            ImGui::TextUnformatted(std::filesystem::path(path).filename().string().c_str());
            ImGui::PopFont();
            if (!entry) {
                ImGui::TextDisabled("<no inspector for this file type>");
                return;
            }
            ImGui::TextDisabled("%s", entry->DisplayName.c_str());
            ImGui::Separator();
            ImGui::PushID("AssetFields");
            entry->DrawInspector(path, ctx);
            ImGui::PopID();
        }

        // One script slot's DUALITY_PROPERTY / @field fields. While Play is running,
        // `script.Instance` is live and edits go straight to it (lost on Stop, Unity semantics).
        // In Edit mode a scratch instance is created for this frame, seeded from
        // PropertyOverrides, drawn, and destroyed; edits are captured back into PropertyOverrides.
        void DrawScriptFields(EditorContext& ctx, ScriptInstance& script) {
            ScriptRegistry::ResolvePendingOverrides(script);
            const std::vector<FieldHandle>& fields = ScriptRegistry::GetFields(script.ClassName);
            if (fields.empty()) {
                if (!ScriptRegistry::IsRegistered(script.ClassName))
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s  Script class '%s' is not compiled -- check the Console", EditorIcons::Warning, script.ClassName.c_str());
                return;
            }

            Behaviour* target = script.Instance;
            Behaviour* scratch = nullptr;
            void (*destroyScratch)(Behaviour*) = nullptr;
            if (!target && ScriptRegistry::TryCreate(script.ClassName, &scratch, &destroyScratch)) {
                for (auto& field : fields) {
                    auto it = script.PropertyOverrides.find(field.Name);
                    if (it != script.PropertyOverrides.end())
                        field.Set(scratch, it->second);
                }
                target = scratch;
            }

            if (target) {
                for (auto& field : fields) {
                    if (DrawFieldWidget(field, target, &ctx.SceneRef)) {
                        script.PropertyOverrides[field.Name] = field.Get(target);
                        MarkSceneDirty(ctx, ("Modify " + script.ClassName + "." + field.Name).c_str());
                    }
                }
            }

            if (scratch)
                destroyScratch(scratch);
        }

        // Unity-style "Anchor Presets" quick-set (3x3 point anchors).
        // Unity shows nothing for a MeshRenderer without a MeshFilter; say why, with a fix.
        void DrawMeshFilterHelpBox(EditorContext& ctx, const std::vector<Entity>& targets) {
            bool anyMissing = false;
            for (Entity entity : targets)
                anyMissing |= !entity.HasComponent<MeshFilterComponent>();
            if (!anyMissing)
                return;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.28f, 0.10f, 0.55f));
            ImGui::BeginChild("##meshFilterHelp", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f), ImGuiChildFlags_Borders);
            ImGui::TextWrapped("%s  This Mesh Renderer has no Mesh Filter, so it draws nothing. Add a Mesh Filter and pick a mesh.", EditorIcons::Warning);
            if (ImGui::SmallButton("Add Mesh Filter")) {
                for (Entity entity : targets)
                    if (!entity.HasComponent<MeshFilterComponent>())
                        entity.AddComponent<MeshFilterComponent>();
                MarkSceneDirty(ctx, "Add Mesh Filter");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawUIRectAnchorPresets(UIRectComponent* rect, EditorContext& ctx) {
            static const UIAnchor kGrid[3][3] = {
                { UIAnchor::TopLeft,    UIAnchor::TopCenter,    UIAnchor::TopRight },
                { UIAnchor::MiddleLeft, UIAnchor::MiddleCenter, UIAnchor::MiddleRight },
                { UIAnchor::BottomLeft, UIAnchor::BottomCenter, UIAnchor::BottomRight },
            };
            BeginPropertyRow("Anchor Presets");
            ImGui::BeginGroup();
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    ImGui::PushID(row * 3 + col);
                    if (col > 0)
                        ImGui::SameLine(0.0f, 2.0f);
                    if (ImGui::Button("##AnchorPreset", ImVec2(18.0f, 18.0f))) {
                        UIAnchorPresetToMinMaxPivot(kGrid[row][col], rect->AnchorMin, rect->AnchorMax, rect->Pivot);
                        MarkSceneDirty(ctx, "Anchor Preset");
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndGroup();
        }

        // Card header bar shared by components and scripts: foldout arrow, icon, optional
        // enable checkbox, bold title, kebab button. Returns whether the card is open; sets
        // `menuRequested` when the kebab was clicked or the header right-clicked.
        bool CardHeader(const char* id, const char* icon, const std::string& title, bool* enabled, bool& enabledChanged,
            bool& menuRequested, bool mixedEnabled = false, const char* badge = nullptr) {
            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.29f, 0.29f, 0.29f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.29f, 0.29f, 0.29f, 1.0f));
            const bool open = ImGui::CollapsingHeader(id, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap |
                ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding);
            ImGui::PopStyleColor(3);
            menuRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float textY = min.y + (max.y - min.y - ImGui::GetFontSize()) * 0.5f;
            float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
            drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(ImGuiCol_Text), icon);
            x += ImGui::GetFontSize() + 6.0f;
            enabledChanged = false;
            if (enabled) {
                const float boxSize = ImGui::GetFontSize();
                ImGui::SetCursorScreenPos(ImVec2(x, min.y + (max.y - min.y - boxSize) * 0.5f - 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 1.0f));
                if (mixedEnabled)
                    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                enabledChanged = ImGui::Checkbox("##enabled", enabled);
                if (mixedEnabled)
                    ImGui::PopItemFlag();
                ImGui::PopStyleVar();
                x += boxSize + 8.0f;
            }
            ImGui::PushFont(EditorBoldFont());
            drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
            x += ImGui::CalcTextSize(title.c_str()).x;
            ImGui::PopFont();
            if (badge) {
                drawList->AddText(ImVec2(x + 8.0f, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
            }
            const float buttonWidth = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(max.x - buttonWidth - style.FramePadding.x, min.y));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            if (ImGui::Button(EditorIcons::Kebab, ImVec2(buttonWidth, max.y - min.y)))
                menuRequested = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(after);
            return open;
        }

        nlohmann::json ComponentToJson(ComponentTypeInfo& type, Entity entity) {
            nlohmann::json values = nlohmann::json::object();
            void* component = type.GetPtr(entity);
            for (const FieldHandle& field : type.Fields)
                values[field.Name] = FieldValueToJson(field.Get(component));
            return values;
        }

        void JsonToComponent(ComponentTypeInfo& type, Entity entity, const nlohmann::json& values) {
            void* component = type.GetPtr(entity);
            for (const FieldHandle& field : type.Fields) {
                if (values.contains(field.Name))
                    field.Set(component, JsonToFieldValue(values[field.Name], field.Get(component)));
            }
        }
    }

    // ---------------------------------------------------------------------------- Prefabs

    const nlohmann::json* PropertiesPanel::PrefabEntityJson(EditorContext& ctx, Entity entity, bool& isRoot) {
        (void)ctx;
        isRoot = false;
        Entity root = PrefabRootOf(entity);
        if (!root)
            return nullptr;
        // .prefab itself, or the generated prefab of a model instance (overrides compare against it).
        const std::string path = ModelAsset::ResolveInstantiablePrefab(root.GetComponent<PrefabInstanceComponent>().Prefab.Guid);
        if (path.empty())
            return nullptr;
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(path, error);
        if (error)
            return nullptr;
        if (path != m_PrefabCachePath || writeTime != m_PrefabCacheTime) {
            m_PrefabCachePath = path;
            m_PrefabCacheTime = writeTime;
            std::ifstream file(path);
            m_PrefabCache = nlohmann::json::parse(file, nullptr, false);
            // Legacy pixel-unit prefabs are migrated at instantiation; comparing against the raw
            // file would flag every value, so they are not compared at all.
            if (!m_PrefabCache.is_object() || m_PrefabCache.value("WorldUnitsVersion", 1) < 2)
                m_PrefabCache = nlohmann::json();
        }
        if (!m_PrefabCache.is_object() || !m_PrefabCache.contains("Entities"))
            return nullptr;
        std::vector<Entity> order;
        CollectPreOrder(root, order);
        auto found = std::find(order.begin(), order.end(), entity);
        if (found == order.end())
            return nullptr;
        const size_t index = static_cast<size_t>(found - order.begin());
        const nlohmann::json& entities = m_PrefabCache["Entities"];
        if (index >= entities.size() || order.size() != entities.size())
            return nullptr;
        isRoot = index == 0;
        return &entities[index];
    }

    int PropertiesPanel::CountPrefabOverrides(EditorContext& ctx, Entity root) {
        std::vector<Entity> order;
        CollectPreOrder(root, order);
        int count = 0;
        for (Entity entity : order) {
            bool isRoot = false;
            const nlohmann::json* source = PrefabEntityJson(ctx, entity, isRoot);
            if (!source)
                continue;
            for (ComponentTypeInfo& type : TypeRegistry::All()) {
                if (!type.Has(entity) || type.DisplayName == "Prefab Instance" || type.DisplayName == "Scripts")
                    continue;
                if (!source->contains(type.DisplayName)) {
                    ++count; // added component
                    continue;
                }
                const nlohmann::json& values = (*source)[type.DisplayName];
                void* component = type.GetPtr(entity);
                for (const FieldHandle& field : type.Fields) {
                    if ((isRoot && IsRootOnlyField(type.DisplayName, field.Name)) || !values.contains(field.Name))
                        continue;
                    const FieldValue value = field.Get(component);
                    if (std::holds_alternative<EntityRef>(value) || std::holds_alternative<std::vector<EntityRef>>(value))
                        continue;
                    if (!JsonNearlyEqual(FieldValueToJson(value), values[field.Name]))
                        ++count;
                }
            }
        }
        if (m_PrefabCache.is_object() && m_PrefabCache.contains("Entities") && m_PrefabCache["Entities"].size() != order.size())
            ++count; // structural change (added / removed children)
        return count;
    }

    void PropertiesPanel::DrawPrefabRow(EditorContext& ctx, Entity primary) {
        Entity root = PrefabRootOf(primary);
        if (!root)
            return;
        const std::string path = AssetDatabase::ResolvePath(root.GetComponent<PrefabInstanceComponent>().Prefab.Guid);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(path.empty() ? EditorColors::PrefabMissing() : EditorColors::PrefabText()));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(path.empty() ? "Prefab (Missing)" : "Prefab");
        ImGui::PopStyleColor();
        ImGui::SameLine(FieldLabelWidth());
        ImGui::BeginDisabled(path.empty());
        // A model prefab (FBX/glTF instance) is read-only like Unity's: no Prefab Mode.
        ImGui::BeginDisabled(ModelAsset::IsModelFile(path));
        if (ImGui::Button("Open"))
            ctx.RequestOpenPrefabPath = path;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Select")) {
            ctx.Session.PingAssetPath = path;
            ctx.Session.PingStartTime = ImGui::GetTime();
        }
        ImGui::EndDisabled();
        if (root != primary) {
            ImGui::SameLine();
            ImGui::TextDisabled("(in %s)", root.GetComponent<NameComponent>().Name.c_str());
            return;
        }
        ImGui::SameLine();
        const int overrides = path.empty() ? 0 : CountPrefabOverrides(ctx, root);
        const std::string overridesLabel = "Overrides" + (overrides > 0 ? " (" + std::to_string(overrides) + ")" : std::string()) + " " + EditorIcons::CaretDown;
        if (ImGui::Button(overridesLabel.c_str()))
            ImGui::OpenPopup("PrefabOverrides");
        if (ImGui::BeginPopup("PrefabOverrides")) {
            ImGui::PushFont(EditorBoldFont());
            ImGui::Text("Review, Revert or Apply Overrides");
            ImGui::PopFont();
            ImGui::TextDisabled("on '%s'", std::filesystem::path(path).filename().string().c_str());
            ImGui::Separator();
            if (overrides == 0)
                ImGui::TextDisabled("No Overrides");
            else
                ImGui::Text("%d overridden value(s) -- shown in bold in the Inspector.", overrides);
            ImGui::Spacing();
            if (ImGui::Button("Revert All", ImVec2(130.0f, 0.0f))) {
                Entity replacement = PrefabSerializer::Revert(ctx.SceneRef, root);
                if (replacement) {
                    EditorCommands::SelectOnly(ctx, replacement);
                    MarkSceneDirty(ctx, "Revert Prefab");
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply All", ImVec2(130.0f, 0.0f))) {
                if (PrefabSerializer::Apply(root)) {
                    m_PrefabCachePath.clear();
                    Log::Info("Applied prefab overrides to '" + path + "'");
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Unpack Prefab")) {
                if (PrefabSerializer::Unpack(root))
                    MarkSceneDirty(ctx, "Unpack Prefab");
            }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------------------ Header

    void PropertiesPanel::DrawEntityHeader(EditorContext& ctx, const std::vector<Entity>& targets) {
        Entity primary = targets.front();
        const bool multi = targets.size() > 1;

        // Row 1: icon, active toggle, name (Unity's GameObject header).
        ImGui::PushFont(EditorBoldFont());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(EditorIcons::Cube);
        ImGui::PopFont();
        ImGui::SameLine();
        bool active = primary.GetComponent<ActiveComponent>().Active;
        bool mixedActive = false;
        for (Entity entity : targets)
            mixedActive |= entity.GetComponent<ActiveComponent>().Active != active;
        if (mixedActive)
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        if (ImGui::Checkbox("##EntityActive", &active)) {
            for (Entity entity : targets)
                EditorCommands::SetActive(ctx, entity, active);
        }
        if (mixedActive)
            ImGui::PopItemFlag();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Active (Alt+Shift+A)");
        ImGui::SameLine();

        bool mixedName = false;
        const std::string& name = primary.GetComponent<NameComponent>().Name;
        for (Entity entity : targets)
            mixedName |= entity.GetComponent<NameComponent>().Name != name;
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s", mixedName ? "" : name.c_str());
        ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        ImGui::PushFont(EditorBoldFont());
        if (ImGui::InputTextWithHint("##EntityName", mixedName ? "\xE2\x80\x94" : "", buffer, sizeof(buffer))) {
            for (Entity entity : targets)
                entity.GetComponent<NameComponent>().Name = buffer;
            MarkSceneDirty(ctx, "Rename");
        }
        ImGui::PopFont();
        ImGui::SameLine();
        // Inspector lock (Unity's padlock).
        ImGui::PushStyleColor(ImGuiCol_Button, m_Locked ? ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()) : ImVec4(0, 0, 0, 0));
        if (ImGui::Button(m_Locked ? EditorIcons::Lock : EditorIcons::Unlock, ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
            m_Locked = !m_Locked;
            if (m_Locked) {
                m_LockedIds = ctx.Session.Ids.IdsOf(targets);
                m_LockedAssetPath.clear();
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(m_Locked ? "Unlock the Inspector" : "Lock the Inspector to this selection");

        // Row 2: Tag and Layer.
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const std::string& tag = primary.GetComponent<TagComponent>().Tag;
        bool mixedTag = false;
        for (Entity entity : targets)
            mixedTag |= entity.GetComponent<TagComponent>().Tag != tag;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Tag");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half - ImGui::CalcTextSize("Tag").x - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##Tag", mixedTag ? "\xE2\x80\x94" : tag.c_str())) {
            std::set<std::string> tags = { "Untagged", "Player", "MainCamera", "EditorOnly", "Respawn", "Finish", "GameController" };
            for (auto handle : ctx.SceneRef.Registry().view<TagComponent>())
                tags.insert(ctx.SceneRef.Registry().get<TagComponent>(handle).Tag);
            for (const std::string& option : tags) {
                if (ImGui::Selectable(option.c_str(), option == tag)) {
                    for (Entity entity : targets)
                        entity.GetComponent<TagComponent>().Tag = option;
                    MarkSceneDirty(ctx, "Change Tag");
                }
            }
            ImGui::Separator();
            static char newTag[64] = "";
            ImGui::SetNextItemWidth(140.0f);
            const bool add = ImGui::InputTextWithHint("##NewTag", "New tag", newTag, sizeof(newTag), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::Button("Add Tag") || add) && newTag[0] != '\0') {
                for (Entity entity : targets)
                    entity.GetComponent<TagComponent>().Tag = newTag;
                MarkSceneDirty(ctx, "Change Tag");
                newTag[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Layer");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        auto layerLabel = [&](Entity entity) -> std::string {
            if (entity.HasComponent<LayerComponent>())
                return LayerName(entity.GetComponent<LayerComponent>().Value);
            return std::string("Inherit (") + LayerName(ctx.SceneRef.ResolveEntityLayer(entity)) + ")";
        };
        const std::string currentLayer = layerLabel(primary);
        bool mixedLayer = false;
        for (Entity entity : targets)
            mixedLayer |= layerLabel(entity) != currentLayer;
        if (ImGui::BeginCombo("##Layer", mixedLayer ? "\xE2\x80\x94" : currentLayer.c_str())) {
            if (ImGui::Selectable("Inherit from parent", !primary.HasComponent<LayerComponent>())) {
                for (Entity entity : targets)
                    if (entity.HasComponent<LayerComponent>())
                        entity.RemoveComponent<LayerComponent>();
                MarkSceneDirty(ctx, "Change Layer");
            }
            ImGui::Separator();
            for (Layer layer : { Layer::Default, Layer::TOP, Layer::BOTTOM }) {
                const bool selectedLayer = primary.HasComponent<LayerComponent>() && primary.GetComponent<LayerComponent>().Value == layer;
                if (ImGui::Selectable(LayerName(layer), selectedLayer)) {
                    for (Entity entity : targets) {
                        auto& component = entity.HasComponent<LayerComponent>() ? entity.GetComponent<LayerComponent>() : entity.AddComponent<LayerComponent>();
                        component.Value = layer;
                    }
                    MarkSceneDirty(ctx, "Change Layer");
                }
            }
            ImGui::EndCombo();
        }

        if (!multi)
            DrawPrefabRow(ctx, primary);
        else
            ImGui::TextDisabled("%zu objects selected -- showing the components they share", targets.size());
        ImGui::Spacing();
    }

    // ---------------------------------------------------------------------- Component cards

    bool PropertiesPanel::DrawComponentMenu(EditorContext& ctx, ComponentTypeInfo& type, const std::vector<Entity>& targets) {
        bool removed = false;
        if (ImGui::MenuItem("Reset")) {
            // Default values come from a freshly-added component of the same type.
            Scene scratch;
            Entity defaults = scratch.CreateEntity();
            type.AddDefault(defaults);
            const nlohmann::json values = ComponentToJson(type, defaults);
            for (Entity entity : targets) {
                // Keep identity-like fields (a camera's screen...) only when Unity would? Reset
                // means all fields back to their defaults.
                JsonToComponent(type, entity, values);
            }
            MarkSceneDirty(ctx, ("Reset " + type.DisplayName).c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove Component", nullptr, false, !type.Mandatory)) {
            for (Entity entity : targets)
                if (type.Has(entity))
                    type.Remove(entity);
            MarkSceneDirty(ctx, ("Remove " + type.DisplayName).c_str());
            removed = true;
        }
        ImGui::BeginDisabled();
        ImGui::MenuItem("Move Up");
        ImGui::MenuItem("Move Down");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Built-in components keep a fixed order; scripts can be reordered.");
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Component")) {
            m_CopiedComponentType = type.DisplayName;
            m_CopiedComponent = ComponentToJson(type, targets.front());
        }
        const bool canPaste = m_CopiedComponentType == type.DisplayName;
        if (ImGui::MenuItem("Paste Component Values", nullptr, false, canPaste)) {
            for (Entity entity : targets)
                JsonToComponent(type, entity, m_CopiedComponent);
            MarkSceneDirty(ctx, ("Paste " + type.DisplayName + " Values").c_str());
        }
        if (!m_CopiedComponentType.empty()) {
            ComponentTypeInfo* copiedType = TypeRegistry::Find(m_CopiedComponentType);
            bool anyMissing = false;
            for (Entity entity : targets)
                anyMissing |= copiedType && !copiedType->Has(entity);
            if (ImGui::MenuItem(("Paste Component As New (" + m_CopiedComponentType + ")").c_str(), nullptr, false, anyMissing)) {
                for (Entity entity : targets) {
                    if (copiedType->Has(entity))
                        continue;
                    copiedType->AddDefault(entity);
                    JsonToComponent(*copiedType, entity, m_CopiedComponent);
                }
                MarkSceneDirty(ctx, ("Paste " + m_CopiedComponentType + " As New").c_str());
            }
        }
        return removed;
    }

    void PropertiesPanel::DrawComponentCard(EditorContext& ctx, ComponentTypeInfo& type, const std::vector<Entity>& targets) {
        Entity primary = targets.front();
        void* component = type.GetPtr(primary);
        ImGui::PushID(type.DisplayName.c_str());

        // Prefab override data for the primary (single selection only).
        bool isRoot = false;
        const nlohmann::json* prefabEntity = targets.size() == 1 ? PrefabEntityJson(ctx, primary, isRoot) : nullptr;
        const nlohmann::json* prefabValues = (prefabEntity && prefabEntity->contains(type.DisplayName)) ? &(*prefabEntity)[type.DisplayName] : nullptr;
        const bool addedOverride = prefabEntity && !prefabValues;

        FieldHandle* enabledField = FindEnabledField(type);
        bool enabled = enabledField ? std::get<bool>(enabledField->Get(component)) : false;
        bool mixedEnabled = false;
        if (enabledField)
            for (Entity entity : targets)
                mixedEnabled |= std::get<bool>(enabledField->Get(type.GetPtr(entity))) != enabled;
        bool enabledChanged = false, menuRequested = false;
        const std::string title = type.DisplayName + (addedOverride ? "  (+)" : "");
        const bool open = CardHeader(("##card_" + type.DisplayName).c_str(), ComponentIcon(type.DisplayName), title,
            enabledField ? &enabled : nullptr, enabledChanged, menuRequested, mixedEnabled);
        if (enabledChanged) {
            for (Entity entity : targets)
                enabledField->Set(type.GetPtr(entity), enabled);
            MarkSceneDirty(ctx, ((enabled ? "Enable " : "Disable ") + type.DisplayName).c_str());
        }
        if (menuRequested)
            ImGui::OpenPopup("ComponentMenu");
        bool removed = false;
        if (ImGui::BeginPopup("ComponentMenu")) {
            removed = DrawComponentMenu(ctx, type, targets);
            ImGui::EndPopup();
        }

        if (open && !removed) {
            ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
            ImGui::PushID("Fields");
            if (type.DisplayName == "UI Rect" && targets.size() == 1)
                DrawUIRectAnchorPresets(static_cast<UIRectComponent*>(component), ctx);
            if (type.DisplayName == "Mesh Renderer")
                DrawMeshFilterHelpBox(ctx, targets);
            for (auto& field : type.Fields) {
                if (field.Name == "Enabled")
                    continue; // drawn on the header
                FieldDrawState state;
                state.MixedMask = MultiEdit::MixedMask(field, type, targets);
                if (prefabValues && prefabValues->contains(field.Name) && !(isRoot && IsRootOnlyField(type.DisplayName, field.Name))) {
                    const FieldValue current = field.Get(component);
                    if (!std::holds_alternative<EntityRef>(current) && !std::holds_alternative<std::vector<EntityRef>>(current) &&
                        !JsonNearlyEqual(FieldValueToJson(current), (*prefabValues)[field.Name])) {
                        state.Overridden = true;
                        const nlohmann::json original = (*prefabValues)[field.Name];
                        const FieldHandle* fieldPtr = &field;
                        EditorContext* context = &ctx;
                        state.ContextMenu = [fieldPtr, original, component, context, &type]() {
                            if (ImGui::MenuItem("Revert")) {
                                fieldPtr->Set(component, JsonToFieldValue(original, fieldPtr->Get(component)));
                                MarkSceneDirty(*context, ("Revert " + type.DisplayName + "." + fieldPtr->Name).c_str());
                            }
                        };
                    }
                } else if (addedOverride) {
                    state.Overridden = true;
                }
                const nlohmann::json before = FieldValueToJson(field.Get(component));
                SetNextFieldDrawState(std::move(state));
                if (DrawFieldWidget(field, component, &ctx.SceneRef)) {
                    if (targets.size() > 1)
                        MultiEdit::Propagate(field, type, targets, before, FieldValueToJson(field.Get(component)));
                    MarkSceneDirty(ctx, ("Modify " + type.DisplayName + " " + field.Name).c_str());
                }
            }
            ImGui::PopID();
            ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
            ImGui::Spacing();
        }
        ImGui::PopID();
    }

    void PropertiesPanel::DrawScriptCards(EditorContext& ctx, Entity entity) {
        BehaviourComponent& behaviour = entity.GetComponent<BehaviourComponent>();
        int removeIndex = -1;
        int moveFrom = -1, moveTo = -1;
        for (int i = 0; i < static_cast<int>(behaviour.Scripts.size()); i++) {
            ScriptInstance& script = behaviour.Scripts[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const bool isTypeScript = ScriptRegistry::GetLanguage(script.ClassName) == ScriptLanguage::TypeScript;
            bool enabled = script.Enabled;
            bool enabledChanged = false, menuRequested = false;
            const bool open = CardHeader(("##script_" + script.ClassName).c_str(), EditorIcons::Code, script.ClassName + " (Script)",
                &enabled, enabledChanged, menuRequested, false, isTypeScript ? "TS" : "C++");
            if (enabledChanged) {
                script.Enabled = enabled;
                MarkSceneDirty(ctx, ((enabled ? "Enable " : "Disable ") + script.ClassName).c_str());
            }
            if (menuRequested)
                ImGui::OpenPopup("ScriptMenu");
            if (ImGui::BeginPopup("ScriptMenu")) {
                if (ImGui::MenuItem("Reset")) {
                    script.PropertyOverrides.clear();
                    script.UnresolvedOverridesJson.clear();
                    MarkSceneDirty(ctx, ("Reset " + script.ClassName).c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Component"))
                    removeIndex = i;
                if (ImGui::MenuItem("Move Up", nullptr, false, i > 0)) {
                    moveFrom = i;
                    moveTo = i - 1;
                }
                if (ImGui::MenuItem("Move Down", nullptr, false, i + 1 < static_cast<int>(behaviour.Scripts.size()))) {
                    moveFrom = i;
                    moveTo = i + 1;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy Component")) {
                    m_CopiedComponentType = "Script:" + script.ClassName;
                    m_CopiedComponent = nlohmann::json::object();
                    for (const auto& [fieldName, value] : script.PropertyOverrides)
                        m_CopiedComponent[fieldName] = FieldValueToJson(value);
                }
                if (ImGui::MenuItem("Paste Component Values", nullptr, false, m_CopiedComponentType == "Script:" + script.ClassName)) {
                    script.UnresolvedOverridesJson = m_CopiedComponent.dump();
                    script.PropertyOverrides.clear();
                    ScriptRegistry::ResolvePendingOverrides(script);
                    MarkSceneDirty(ctx, ("Paste " + script.ClassName + " Values").c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Edit Script")) {
                    const std::string source = FindScriptSource(ctx, script.ClassName, isTypeScript);
                    if (source.empty())
                        Log::Warn("Could not find the source file of script '" + script.ClassName + "'");
                    else
                        OpenInShell(source);
                }
                ImGui::EndPopup();
            }
            if (open) {
                ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
                DrawScriptFields(ctx, script);
                ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
                ImGui::Spacing();
            }
            ImGui::PopID();
        }
        if (moveFrom >= 0) {
            std::swap(behaviour.Scripts[static_cast<size_t>(moveFrom)], behaviour.Scripts[static_cast<size_t>(moveTo)]);
            MarkSceneDirty(ctx, "Reorder Scripts");
        }
        if (removeIndex >= 0) {
            const std::string name = behaviour.Scripts[static_cast<size_t>(removeIndex)].ClassName;
            behaviour.Scripts.erase(behaviour.Scripts.begin() + removeIndex);
            if (behaviour.Scripts.empty())
                entity.RemoveComponent<BehaviourComponent>();
            MarkSceneDirty(ctx, ("Remove " + name).c_str());
        }
    }

    // ------------------------------------------------------------------------ Add Component

    void PropertiesPanel::DrawAddComponent(EditorContext& ctx, const std::vector<Entity>& targets) {
        const float buttonWidth = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f);
        const ImVec2 buttonPos = ImGui::GetCursorScreenPos();
        if (ImGui::Button("Add Component", ImVec2(buttonWidth, ImGui::GetFrameHeight() * 1.2f))) {
            m_AddComponentFilter[0] = '\0';
            m_AddComponentCategory.clear();
            m_NewScriptPage = false;
            ImGui::OpenPopup("AddComponentPopup");
        }
        // Below the button like Unity, or above it when the Inspector has no room underneath.
        const float popupHeight = 320.0f + ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
        const ImGuiViewport* viewport = ImGui::GetWindowViewport();
        const float below = buttonPos.y + ImGui::GetFrameHeight() * 1.2f;
        if (below + popupHeight > viewport->WorkPos.y + viewport->WorkSize.y)
            ImGui::SetNextWindowPos(buttonPos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        else
            ImGui::SetNextWindowPos(ImVec2(buttonPos.x, below));
        ImGui::SetNextWindowSize(ImVec2(buttonWidth, 0.0f));
        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;

        auto addScript = [&](const std::string& className) {
            for (Entity entity : targets) {
                BehaviourComponent& behaviour = entity.HasComponent<BehaviourComponent>() ? entity.GetComponent<BehaviourComponent>() : entity.AddComponent<BehaviourComponent>();
                behaviour.Scripts.push_back(ScriptInstance{ className });
            }
            MarkSceneDirty(ctx, ("Add " + className).c_str());
            ImGui::CloseCurrentPopup();
        };
        auto addableComponent = [&](ComponentTypeInfo& type) {
            if (type.Category.empty() || type.HeaderComponent)
                return false;
            for (Entity entity : targets)
                if (!type.Has(entity))
                    return true;
            return false;
        };
        auto componentItem = [&](ComponentTypeInfo& type, bool showCategory) {
            std::string label = std::string(ComponentIcon(type.DisplayName)) + "  " + type.DisplayName;
            if (ImGui::Selectable(label.c_str())) {
                EditorCommands::AddComponentToSelection(ctx, type);
                ImGui::CloseCurrentPopup();
            }
            if (showCategory) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(EditorCommands::ComponentCategoryOf(type.DisplayName)).x);
                ImGui::TextDisabled("%s", EditorCommands::ComponentCategoryOf(type.DisplayName));
            }
        };
        auto scriptItem = [&](const std::string& className) {
            const bool ts = ScriptRegistry::GetLanguage(className) == ScriptLanguage::TypeScript;
            const std::string label = std::string(EditorIcons::Code) + "  " + className + (ts ? "  (TS)" : "  (C++)") + "##add_" + className;
            if (ImGui::Selectable(label.c_str()))
                addScript(className);
        };

        if (m_NewScriptPage) {
            if (ImGui::Selectable((std::string("<  ") + "New Script").c_str()))
                m_NewScriptPage = false;
            ImGui::Separator();
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            const bool submitted = ImGui::InputText("##NewScriptName", m_NewScriptName, sizeof(m_NewScriptName), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::RadioButton("TypeScript", &m_NewScriptLanguage, 1);
            ImGui::SameLine();
            ImGui::RadioButton("C++", &m_NewScriptLanguage, 0);
            const std::string className = m_NewScriptName;
            const bool valid = IsCppIdentifier(className) && !ScriptRegistry::IsRegistered(className);
            if (!IsCppIdentifier(className))
                ImGui::TextDisabled("Letters, digits and _ (not starting with a digit).");
            else if (ScriptRegistry::IsRegistered(className))
                ImGui::TextDisabled("A script with this name already exists.");
            else
                ImGui::TextDisabled((m_NewScriptLanguage == 1) ? "Assets/Scripts/%s.ts (compiled automatically)" : "Assets/Scripts/%s.h/.cpp (Reload Scripts to compile)", className.c_str());
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("Create and Add", ImVec2(-1.0f, 0.0f)) || (submitted && valid)) {
                auto project = Project::GetActive();
                if (project) {
                    const std::filesystem::path directory = project->GetScriptsDirectory();
                    std::filesystem::create_directories(directory);
                    const bool created = (m_NewScriptLanguage == 1) ? CreateTypeScriptAsset(directory, className) : CreateScriptAsset(directory, className);
                    if (created) {
                        Log::Info("Created script '" + className + "' -- it becomes editable here once compiled.");
                        addScript(className);
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
            return;
        }

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AddComponentFilter", (std::string(EditorIcons::Search) + " Search").c_str(), m_AddComponentFilter, sizeof(m_AddComponentFilter));
        ImGui::Separator();
        ImGui::BeginChild("##AddComponentList", ImVec2(0.0f, 320.0f));

        std::vector<std::string> scripts = ScriptRegistry::GetAllClassNames();
        std::sort(scripts.begin(), scripts.end());

        if (m_AddComponentFilter[0] != '\0') {
            bool any = false;
            for (ComponentTypeInfo& type : TypeRegistry::All()) {
                if (addableComponent(type) && ContainsCaseInsensitive(type.DisplayName, m_AddComponentFilter)) {
                    componentItem(type, true);
                    any = true;
                }
            }
            for (const std::string& className : scripts) {
                if (ContainsCaseInsensitive(className, m_AddComponentFilter)) {
                    scriptItem(className);
                    any = true;
                }
            }
            if (!any)
                ImGui::TextDisabled("No match.");
            if (IsCppIdentifier(m_AddComponentFilter) && !ScriptRegistry::IsRegistered(m_AddComponentFilter)) {
                ImGui::Separator();
                if (ImGui::Selectable((std::string(EditorIcons::Add) + "  New script '" + m_AddComponentFilter + "'").c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                    std::snprintf(m_NewScriptName, sizeof(m_NewScriptName), "%s", m_AddComponentFilter);
                    m_NewScriptPage = true;
                }
            }
        } else if (m_AddComponentCategory.empty()) {
            ImGui::PushFont(EditorBoldFont());
            ImGui::TextUnformatted("Component");
            ImGui::PopFont();
            for (const char* category : EditorCommands::ComponentCategories()) {
                bool hasAny = false;
                for (ComponentTypeInfo& type : TypeRegistry::All())
                    hasAny |= addableComponent(type) && std::string(EditorCommands::ComponentCategoryOf(type.DisplayName)) == category;
                if (!hasAny)
                    continue;
                if (ImGui::Selectable(category, false, ImGuiSelectableFlags_DontClosePopups))
                    m_AddComponentCategory = category;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(EditorIcons::ChevronRight).x);
                ImGui::TextDisabled("%s", EditorIcons::ChevronRight);
            }
            if (ImGui::Selectable("Scripts", false, ImGuiSelectableFlags_DontClosePopups))
                m_AddComponentCategory = "Scripts";
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(EditorIcons::ChevronRight).x);
            ImGui::TextDisabled("%s", EditorIcons::ChevronRight);
            ImGui::Separator();
            if (ImGui::Selectable((std::string(EditorIcons::Add) + "  New Script").c_str(), false, ImGuiSelectableFlags_DontClosePopups))
                m_NewScriptPage = true;
        } else {
            ImGui::PushFont(EditorBoldFont());
            if (ImGui::Selectable(("<  " + m_AddComponentCategory).c_str(), false, ImGuiSelectableFlags_DontClosePopups))
                m_AddComponentCategory.clear();
            ImGui::PopFont();
            ImGui::Separator();
            if (m_AddComponentCategory == "Scripts") {
                if (scripts.empty())
                    ImGui::TextDisabled("No compiled scripts yet.");
                for (const std::string& className : scripts)
                    scriptItem(className);
            } else {
                for (ComponentTypeInfo& type : TypeRegistry::All()) {
                    if (addableComponent(type) && m_AddComponentCategory == EditorCommands::ComponentCategoryOf(type.DisplayName))
                        componentItem(type, false);
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    // ------------------------------------------------------------------------------ Window

    void PropertiesPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.Session.ShowInspector)
            return;
        if (!ImGui::Begin("Inspector", &ctx.Session.ShowInspector)) {
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ctx.Session.FocusedPanel = EditorPanel::Inspector;

        // Targets: the locked set, or the live selection (primary first).
        std::vector<Entity> targets;
        if (m_Locked) {
            targets = ctx.Session.Ids.Resolve(ctx.SceneRef, m_LockedIds);
            if (targets.empty() && m_LockedAssetPath.empty())
                m_Locked = false; // the locked objects are gone (scene switch)
        }
        if (!m_Locked) {
            if (IsLive(ctx, ctx.Selected)) {
                targets.push_back(ctx.Selected);
                for (Entity entity : ctx.SelectedEntities)
                    if (entity != ctx.Selected && IsLive(ctx, entity))
                        targets.push_back(entity);
            }
        }
        const std::string selectedAssetPath = m_Locked ? m_LockedAssetPath : ctx.SelectedAssetPath;

        if (targets.empty()) {
            if (!selectedAssetPath.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Button, m_Locked ? ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()) : ImVec4(0, 0, 0, 0));
                ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
                if (ImGui::Button(m_Locked ? EditorIcons::Lock : EditorIcons::Unlock, ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
                    m_Locked = !m_Locked;
                    m_LockedIds.clear();
                    m_LockedAssetPath = m_Locked ? ctx.SelectedAssetPath : std::string();
                }
                ImGui::PopStyleColor();
                DrawSelectedAsset(ctx, selectedAssetPath);
            }
            ImGui::End();
            return;
        }

        DrawEntityHeader(ctx, targets);

        ImGui::BeginChild("##InspectorComponents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        // Fully generic: adding a component type (or a field) to TypeRegistry never needs changes
        // here. With several targets only the components every target has are shown (Unity).
        for (ComponentTypeInfo& type : TypeRegistry::All()) {
            if (type.HeaderComponent)
                continue;
            bool shared = true;
            for (Entity entity : targets)
                shared &= type.Has(entity);
            if (!shared)
                continue;
            if (type.DisplayName == "Scripts") {
                if (targets.size() == 1)
                    DrawScriptCards(ctx, targets.front());
                else
                    ImGui::TextDisabled("%s  Scripts are edited one object at a time.", EditorIcons::Code);
                continue;
            }
            DrawComponentCard(ctx, type, targets);
            // A Remove inside the card can invalidate later iterations' assumptions.
            if (!IsLive(ctx, targets.front()))
                break;
        }

        // Components not shared by the whole multi-selection (Unity hides them, with a note).
        if (targets.size() > 1) {
            int hidden = 0;
            for (ComponentTypeInfo& type : TypeRegistry::All()) {
                if (type.HeaderComponent)
                    continue;
                bool any = false, all = true;
                for (Entity entity : targets) {
                    any |= type.Has(entity);
                    all &= type.Has(entity);
                }
                hidden += any && !all ? 1 : 0;
            }
            if (hidden > 0)
                ImGui::TextDisabled("%d component(s) not shared by all selected objects are hidden.", hidden);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        DrawAddComponent(ctx, targets);

        // Dropping a script / prefab-less asset is not meaningful; dropping a script class from
        // the Project window adds it (Unity drag-a-script-onto-the-Inspector).
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 40.0f)));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                const std::string path = AssetDatabase::ResolvePath(static_cast<const char*>(payload->Data));
                const std::string ext = LowercaseExtension(path);
                const std::string className = std::filesystem::path(path).stem().string();
                if ((ext == ".ts" || ext == ".h" || ext == ".cpp") && ScriptRegistry::IsRegistered(className)) {
                    for (Entity entity : targets) {
                        BehaviourComponent& behaviour = entity.HasComponent<BehaviourComponent>() ? entity.GetComponent<BehaviourComponent>() : entity.AddComponent<BehaviourComponent>();
                        behaviour.Scripts.push_back(ScriptInstance{ className });
                    }
                    MarkSceneDirty(ctx, ("Add " + className).c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();
        ImGui::End();
    }

}
