#include "DualityEditor/Panels/PropertiesPanel.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "DualityEditor/AssetInspectorRegistry.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    namespace {
        std::string LowercaseExtension(const std::string& path) {
            std::string ext = std::filesystem::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            return ext;
        }

        // Dispatches by extension through AssetInspectorRegistry (see AssetInspectors.cpp)
        // instead of hand-checking each asset type here -- adding a new asset type never needs
        // changes to this function. `path` is whatever PropertiesPanel resolved to show (either
        // the live ctx.SelectedAssetPath, or the locked snapshot -- see OnImGuiRender).
        void DrawSelectedAsset(EditorContext& ctx, const std::string& path) {
            const AssetInspectorEntry* entry = AssetInspectorRegistry::FindByExtension(LowercaseExtension(path));
            if (!entry) {
                ImGui::TextDisabled("<no inspector for this file type>");
                return;
            }
            ImGui::Text("%s", entry->DisplayName.c_str());
            ImGui::Separator();
            ImGui::PushID("AssetFields");
            entry->DrawInspector(path, ctx);
            ImGui::PopID();
        }

        // One script slot's own DUALITY_PROPERTY fields, drawn inside its own card in the
        // Scripts section below -- mirrors Unity showing a MonoBehaviour's public fields right
        // below its script reference. While Play is running, `script.Instance` is a live
        // object; edits go straight to it (and are lost on Stop, matching Unity's own semantics
        // for editing fields during Play). In Edit mode there's no live instance to read/write
        // through FieldHandle::Get/Set, so a scratch instance is created just for this frame,
        // seeded from PropertyOverrides, rendered, and destroyed -- edits are captured back
        // into PropertyOverrides instead.
        void DrawScriptFields(EditorContext& ctx, ScriptInstance& script) {
            const std::vector<FieldHandle>& fields = ScriptRegistry::GetFields(script.ClassName);
            if (fields.empty())
                return;

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
                        MarkSceneDirty(ctx);
                    }
                }
            }

            if (scratch)
                destroyScratch(scratch);
        }

        // Finds the Unity-style component enable field (bool Enabled) if this type has one.
        FieldHandle* FindEnabledField(ComponentTypeInfo& type) {
            for (auto& field : type.Fields) {
                if (field.Name == "Enabled")
                    return &field;
            }
            return nullptr;
        }

        // Unity-style "Anchor Presets" quick-set: a 3x3 grid matching the legacy UIAnchor grid,
        // snapping AnchorMin/AnchorMax/Pivot to a point anchor at the clicked cell. Deliberately
        // does not touch AnchoredPosition/SizeDelta (matches Unity's own plain, non-Alt-click
        // preset-button behavior) -- the rect will visibly jump to sit at the new anchor point
        // using its existing AnchoredPosition/SizeDelta, same as clicking a preset in Unity.
        void DrawUIRectAnchorPresets(UIRectComponent* rect, EditorContext& ctx) {
            static const UIAnchor kGrid[3][3] = {
                { UIAnchor::TopLeft,    UIAnchor::TopCenter,    UIAnchor::TopRight },
                { UIAnchor::MiddleLeft, UIAnchor::MiddleCenter, UIAnchor::MiddleRight },
                { UIAnchor::BottomLeft, UIAnchor::BottomCenter, UIAnchor::BottomRight },
            };
            ImGui::TextUnformatted("Anchor Presets");
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    ImGui::PushID(row * 3 + col);
                    if (col > 0)
                        ImGui::SameLine();
                    if (ImGui::Button("##AnchorPreset", ImVec2(24.0f, 24.0f))) {
                        UIAnchorPresetToMinMaxPivot(kGrid[row][col], rect->AnchorMin, rect->AnchorMax, rect->Pivot);
                        MarkSceneDirty(ctx);
                    }
                    ImGui::PopID();
                }
            }
        }

        // Renders BehaviourComponent's "Scripts" section -- one collapsible card per attached
        // script slot (own header, own "..." Remove Script popup), instead of the single
        // generic header/fields/remove flow every other TypeRegistry component gets, since one
        // entity can now carry several different scripts at once (see BehaviourComponent's own
        // comment). Erasing the last slot removes the whole BehaviourComponent too, so this
        // section disappears cleanly once empty rather than leaving a vestigial empty header.
        void DrawScriptsSection(Entity selected, BehaviourComponent& behaviour, EditorContext& ctx) {
            if (!ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen))
                return;

            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(behaviour.Scripts.size()); i++) {
                ScriptInstance& script = behaviour.Scripts[i];
                ImGui::PushID(i);

                // Unity-style enable checkbox on the script header row (always visible, even
                // when the card is collapsed) -- toggles ScriptInstance::Enabled.
                if (ImGui::Checkbox("##ScriptEnabled", &script.Enabled))
                    MarkSceneDirty(ctx);
                ImGui::SameLine();

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;
                bool open = ImGui::CollapsingHeader(script.ClassName.c_str(), flags);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
                if (ImGui::Button("...", ImVec2(24.0f, 0.0f)))
                    ImGui::OpenPopup("ScriptSettings");
                if (ImGui::BeginPopup("ScriptSettings")) {
                    if (ImGui::MenuItem("Remove Script"))
                        removeIndex = i;
                    ImGui::EndPopup();
                }

                if (open)
                    DrawScriptFields(ctx, script);

                ImGui::PopID();
            }

            if (removeIndex >= 0) {
                behaviour.Scripts.erase(behaviour.Scripts.begin() + removeIndex);
                if (behaviour.Scripts.empty())
                    selected.RemoveComponent<BehaviourComponent>();
                MarkSceneDirty(ctx);
            }
        }
    }

    void PropertiesPanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Properties");

        // Lock toggle first, before resolving what to show -- so turning it on this same frame
        // immediately freezes the CURRENT selection instead of lagging one frame behind.
        bool wasLocked = m_Locked;
        ImGui::Checkbox("Lock", &m_Locked);
        if (m_Locked && !wasLocked) {
            m_LockedEntity = ctx.Selected;
            m_LockedAssetPath = ctx.SelectedAssetPath;
        }

        // Auto-unlock if the locked entity no longer exists (e.g. a scene swap happened while
        // locked) -- entt::registry::valid() is documented safe to call on any handle regardless
        // of which scene "generation" produced it, so this is a cheap, safe guard against the
        // same stale-Entity-across-a-scene-swap risk this codebase already accepts elsewhere
        // (see README's Known limitations) -- crashing on GetComponent would be worse than
        // silently falling back to the live selection.
        if (m_Locked && m_LockedEntity && !ctx.SceneRef.Registry().valid(m_LockedEntity.Handle()))
            m_Locked = false;

        // At most one of these is ever truthy -- ctx.Selected/ctx.SelectedAssetPath are already
        // mutually exclusive live (ContentBrowserPanel clears ctx.Selected when a file is
        // clicked; no entity-select call site needs to clear ctx.SelectedAssetPath in turn,
        // since this panel prioritizes ctx.Selected whenever it's set), and the locked snapshot
        // below just freezes whichever of the two was live at lock-time, preserving the same
        // invariant.
        Entity selected = m_Locked ? m_LockedEntity : ctx.Selected;
        const std::string& selectedAssetPath = m_Locked ? m_LockedAssetPath : ctx.SelectedAssetPath;

        ImGui::Separator();

        if (!selected) {
            if (!selectedAssetPath.empty())
                DrawSelectedAsset(ctx, selectedAssetPath);
            else
                ImGui::TextDisabled("<no selection>");
            ImGui::End();
            return;
        }

        // Fully generic: adding a new component (or a new field to an
        // existing one) to TypeRegistry never needs changes here.
        for (auto& type : TypeRegistry::All()) {
            if (!type.Has(selected))
                continue;

            ImGui::PushID(type.DisplayName.c_str());

            void* component = type.GetPtr(selected);

            // "Scripts" (BehaviourComponent) gets its own multi-card rendering instead of the
            // generic single-header/fields/remove flow below -- see DrawScriptsSection's own
            // comment for why.
            if (type.DisplayName == "Scripts") {
                DrawScriptsSection(selected, *static_cast<BehaviourComponent*>(component), ctx);
                ImGui::PopID();
                continue;
            }

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;

            // Unity-style enable checkbox on the component header (always visible when
            // collapsed). Field stays out of the body list below so it isn't duplicated.
            if (FieldHandle* enabledField = FindEnabledField(type)) {
                FieldValue value = enabledField->Get(component);
                bool enabled = std::get<bool>(value);
                if (ImGui::Checkbox("##ComponentEnabled", &enabled)) {
                    enabledField->Set(component, enabled);
                    MarkSceneDirty(ctx);
                }
                ImGui::SameLine();
            }

            bool open = ImGui::CollapsingHeader(type.DisplayName.c_str(), flags);

            bool removeRequested = false;
            if (!type.Mandatory) {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
                if (ImGui::Button("...", ImVec2(24.0f, 0.0f)))
                    ImGui::OpenPopup("ComponentSettings");
                if (ImGui::BeginPopup("ComponentSettings")) {
                    if (ImGui::MenuItem("Remove Component"))
                        removeRequested = true;
                    ImGui::EndPopup();
                }
            }

            if (open && !removeRequested) {
                // Distinct sub-scope from the CollapsingHeader above -- without it, a
                // component whose DisplayName matches its own field's Name (e.g.
                // NameComponent's "Name" field, TagComponent's "Tag" field) produces
                // an identical ID for both widgets (same enclosing PushID, same
                // label), which ImGui's debug ID-conflict detector flags and which
                // can corrupt either widget's persistent state (a header's open/
                // closed flag colliding with a text field's edit buffer).
                ImGui::PushID("Fields");
                if (type.DisplayName == "UI Rect")
                    DrawUIRectAnchorPresets(static_cast<UIRectComponent*>(component), ctx);
                for (auto& field : type.Fields) {
                    if (field.Name == "Enabled")
                        continue; // drawn on the header above
                    if (DrawFieldWidget(field, component, &ctx.SceneRef))
                        MarkSceneDirty(ctx);
                }
                ImGui::PopID(); // matches PushID("Fields") above
            }

            ImGui::PopID();

            if (removeRequested) {
                type.Remove(selected);
                MarkSceneDirty(ctx);
            }
        }

        ImGui::Separator();
        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup")) {
            for (auto& type : TypeRegistry::All()) {
                // "Scripts" isn't a single addable thing -- the "Add Script" submenu below picks
                // which script class to attach instead (an entity can carry several at once).
                if (type.DisplayName == "Scripts")
                    continue;
                if (!type.Has(selected) && ImGui::MenuItem(type.DisplayName.c_str())) {
                    type.AddDefault(selected);
                    MarkSceneDirty(ctx);
                }
            }

            ImGui::Separator();
            if (ImGui::BeginMenu("Add Script")) {
                std::vector<std::string> classNames = ScriptRegistry::GetAllClassNames();
                std::sort(classNames.begin(), classNames.end()); // stable order -- the registry's own map has none
                if (classNames.empty())
                    ImGui::TextDisabled("<no scripts registered>");
                for (auto& className : classNames) {
                    if (ImGui::MenuItem(className.c_str())) {
                        BehaviourComponent& behaviour = selected.HasComponent<BehaviourComponent>()
                            ? selected.GetComponent<BehaviourComponent>()
                            : selected.AddComponent<BehaviourComponent>();
                        behaviour.Scripts.push_back(ScriptInstance{ className });
                        MarkSceneDirty(ctx);
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

}
