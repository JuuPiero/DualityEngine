#include "DualityEditor/Panels/PropertiesPanel.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Reflection/TypeRegistry.h"

namespace Duality {

    void PropertiesPanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Properties");

        if (!ctx.Selected) {
            ImGui::TextDisabled("<no selection>");
            ImGui::End();
            return;
        }

        // Fully generic: adding a new component (or a new field to an
        // existing one) to TypeRegistry never needs changes here.
        for (auto& type : TypeRegistry::All()) {
            if (!type.Has(ctx.Selected))
                continue;

            ImGui::PushID(type.DisplayName.c_str());

            void* component = type.GetPtr(ctx.Selected);
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
                for (auto& field : type.Fields) {
                    FieldValue value = field.Get(component);
                    bool changed = false;

                    std::visit([&](auto&& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, glm::vec3>) {
                            changed = ImGui::DragFloat3(field.Name.c_str(), &v.x, 0.5f);
                        } else if constexpr (std::is_same_v<T, glm::vec2>) {
                            changed = ImGui::DragFloat2(field.Name.c_str(), &v.x, 0.5f, 1.0f, 1000.0f);
                        } else if constexpr (std::is_same_v<T, glm::vec4>) {
                            changed = ImGui::DragFloat4(field.Name.c_str(), &v.x, 0.01f);
                        } else if constexpr (std::is_same_v<T, Color4>) {
                            changed = ImGui::ColorEdit4(field.Name.c_str(), &v.Value.x);
                        } else if constexpr (std::is_same_v<T, float>) {
                            changed = ImGui::DragFloat(field.Name.c_str(), &v, 0.1f);
                        } else if constexpr (std::is_same_v<T, int>) {
                            changed = ImGui::DragInt(field.Name.c_str(), &v);
                        } else if constexpr (std::is_same_v<T, bool>) {
                            changed = ImGui::Checkbox(field.Name.c_str(), &v);
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            char buffer[256];
                            std::snprintf(buffer, sizeof(buffer), "%s", v.c_str());
                            if (ImGui::InputText(field.Name.c_str(), buffer, sizeof(buffer))) {
                                v = buffer;
                                changed = true;
                            }
                        } else if constexpr (std::is_same_v<T, Screen>) {
                            const char* items[] = { "Top", "Bottom" };
                            int current = (v == Screen::Top) ? 0 : 1;
                            if (ImGui::Combo(field.Name.c_str(), &current, items, 2)) {
                                v = (current == 0) ? Screen::Top : Screen::Bottom;
                                changed = true;
                            }
                        } else if constexpr (std::is_same_v<T, AssetRef>) {
                            // Drag-drop target only for this pass -- no
                            // inline thumbnail preview (would need
                            // ThumbnailCache threaded in here) and no
                            // "browse" file dialog, just what dragging from
                            // the Content Browser needs.
                            ImGui::PushID(field.Name.c_str());
                            std::string resolvedPath = v.Guid.empty() ? std::string() : AssetDatabase::ResolvePath(v.Guid);
                            std::string filename = resolvedPath.empty() ? "<none>" : std::filesystem::path(resolvedPath).filename().string();
                            ImGui::Text("%s", field.Name.c_str());
                            ImGui::SameLine(120.0f);
                            ImGui::Button(filename.c_str(), ImVec2(v.Guid.empty() ? -1.0f : -32.0f, 0.0f));
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                                    v.Guid.assign(static_cast<const char*>(payload->Data));
                                    changed = true;
                                }
                                ImGui::EndDragDropTarget();
                            }
                            if (!v.Guid.empty()) {
                                ImGui::SameLine();
                                if (ImGui::SmallButton("X")) {
                                    v.Guid.clear();
                                    changed = true;
                                }
                            }
                            ImGui::PopID();
                        }
                        if (changed)
                            field.Set(component, FieldValue(v));
                    }, value);
                }
                ImGui::PopID(); // matches PushID("Fields") above
            }

            ImGui::PopID();

            if (removeRequested)
                type.Remove(ctx.Selected);
        }

        ImGui::Separator();
        if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
            ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup")) {
            for (auto& type : TypeRegistry::All()) {
                if (!type.Has(ctx.Selected) && ImGui::MenuItem(type.DisplayName.c_str()))
                    type.AddDefault(ctx.Selected);
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

}
