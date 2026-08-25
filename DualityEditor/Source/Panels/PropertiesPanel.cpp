#include "DualityEditor/Panels/PropertiesPanel.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "DualityEditor/AssetInspectorRegistry.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FieldEditorWidget.h"
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

        // The script's own DUALITY_PROPERTIES fields, drawn right below BehaviourComponent's
        // "Class" field -- mirrors Unity showing a MonoBehaviour's public fields right below
        // its script reference. While Play is running, `behaviour.Instance` is a live object;
        // edits go straight to it (and are lost on Stop, matching Unity's own semantics for
        // editing fields during Play). In Edit mode there's no live instance to read/write
        // through FieldHandle::Get/Set, so a scratch instance is created just for this frame,
        // seeded from PropertyOverrides, rendered, and destroyed -- edits are captured back
        // into PropertyOverrides instead.
        void DrawScriptProperties(EditorContext& ctx, BehaviourComponent& behaviour) {
            if (behaviour.ClassName.empty())
                return;
            const std::vector<FieldHandle>& fields = ScriptRegistry::GetFields(behaviour.ClassName);
            if (fields.empty())
                return;

            ImGui::Spacing();
            ImGui::TextDisabled("Script Properties");
            ImGui::PushID("ScriptProperties");

            Behaviour* target = behaviour.Instance;
            Behaviour* scratch = nullptr;
            void (*destroyScratch)(Behaviour*) = nullptr;
            if (!target && ScriptRegistry::TryCreate(behaviour.ClassName, &scratch, &destroyScratch)) {
                for (auto& field : fields) {
                    auto it = behaviour.PropertyOverrides.find(field.Name);
                    if (it != behaviour.PropertyOverrides.end())
                        field.Set(scratch, it->second);
                }
                target = scratch;
            }

            if (target) {
                for (auto& field : fields) {
                    if (DrawFieldWidget(field, target, &ctx.SceneRef))
                        behaviour.PropertyOverrides[field.Name] = field.Get(target);
                }
            }

            if (scratch)
                destroyScratch(scratch);

            ImGui::PopID();
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
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap;
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
                for (auto& field : type.Fields)
                    DrawFieldWidget(field, component, &ctx.SceneRef);
                ImGui::PopID(); // matches PushID("Fields") above

                if (type.DisplayName == "Behaviour")
                    DrawScriptProperties(ctx, *static_cast<BehaviourComponent*>(component));
            }

            ImGui::PopID();

            if (removeRequested)
                type.Remove(selected);
        }

        ImGui::Separator();
        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup")) {
            for (auto& type : TypeRegistry::All()) {
                if (!type.Has(selected) && ImGui::MenuItem(type.DisplayName.c_str()))
                    type.AddDefault(selected);
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

}
