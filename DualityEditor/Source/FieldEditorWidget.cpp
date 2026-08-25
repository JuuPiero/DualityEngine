#include "DualityEditor/FieldEditorWidget.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>

#include <imgui.h>

#include "DualityEngine/Asset/AssetDatabase.h"

namespace Duality {

    bool DrawFieldWidget(const FieldHandle& field, void* instance) {
        FieldValue value = field.Get(instance);
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
            } else if constexpr (std::is_same_v<T, ProjectionType>) {
                const char* items[] = { "Orthographic", "Perspective" };
                int current = (v == ProjectionType::Orthographic) ? 0 : 1;
                if (ImGui::Combo(field.Name.c_str(), &current, items, 2)) {
                    v = (current == 0) ? ProjectionType::Orthographic : ProjectionType::Perspective;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, MeshPrimitive>) {
                const char* items[] = { "Cube", "Sphere", "Plane" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(field.Name.c_str(), &current, items, 3)) {
                    v = static_cast<MeshPrimitive>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, UIAnchor>) {
                const char* items[] = { "Top Left", "Top Center", "Top Right", "Middle Left", "Middle Center", "Middle Right", "Bottom Left", "Bottom Center", "Bottom Right" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(field.Name.c_str(), &current, items, 9)) {
                    v = static_cast<UIAnchor>(current);
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
                field.Set(instance, FieldValue(v));
        }, value);

        return changed;
    }

}
