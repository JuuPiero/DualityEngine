#include "DualityEditor/FieldEditorWidget.h"

#include <any>
#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

#include <entt.hpp>
#include <imgui.h>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Renderer/CanvasRenderMode.h"
#include "DualityEngine/Renderer/TextAlignment.h"
#include "DualityEngine/Renderer/UILayoutType.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        constexpr float kNestedFieldIndent = 14.0f;
        constexpr float kFieldLabelOffset = 120.0f;

        float FieldLabelOffset(int indentDepth) {
            return kFieldLabelOffset + indentDepth * kNestedFieldIndent;
        }
    }

    bool DrawFieldWidget(const FieldHandle& field, void* instance, Scene* scene) {
        FieldValue value = field.Get(instance);
        bool changed = DrawFieldValueWidget(field.Name, value, scene);
        if (changed)
            field.Set(instance, value);
        return changed;
    }

    bool DrawFieldValueWidget(const std::string& name, FieldValue& value, Scene* scene, int indentDepth) {
        bool changed = false;

        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, glm::vec3>) {
                changed = ImGui::DragFloat3(name.c_str(), &v.x, 0.5f);
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                changed = ImGui::DragFloat2(name.c_str(), &v.x, 0.5f, 1.0f, 1000.0f);
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                changed = ImGui::DragFloat4(name.c_str(), &v.x, 0.01f);
            } else if constexpr (std::is_same_v<T, Color4>) {
                changed = ImGui::ColorEdit4(name.c_str(), &v.Value.x);
            } else if constexpr (std::is_same_v<T, float>) {
                changed = ImGui::DragFloat(name.c_str(), &v, 0.1f);
            } else if constexpr (std::is_same_v<T, int>) {
                changed = ImGui::DragInt(name.c_str(), &v);
            } else if constexpr (std::is_same_v<T, bool>) {
                changed = ImGui::Checkbox(name.c_str(), &v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), "%s", v.c_str());
                if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer))) {
                    v = buffer;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, Screen>) {
                const char* items[] = { "Top", "Bottom" };
                int current = (v == Screen::Top) ? 0 : 1;
                if (ImGui::Combo(name.c_str(), &current, items, 2)) {
                    v = (current == 0) ? Screen::Top : Screen::Bottom;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, ProjectionType>) {
                const char* items[] = { "Orthographic", "Perspective" };
                int current = (v == ProjectionType::Orthographic) ? 0 : 1;
                if (ImGui::Combo(name.c_str(), &current, items, 2)) {
                    v = (current == 0) ? ProjectionType::Orthographic : ProjectionType::Perspective;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, MeshPrimitive>) {
                const char* items[] = { "Cube", "Sphere", "Plane", "Capsule" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(name.c_str(), &current, items, 4)) {
                    v = static_cast<MeshPrimitive>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, UIAnchor>) {
                const char* items[] = { "Top Left", "Top Center", "Top Right", "Middle Left", "Middle Center", "Middle Right", "Bottom Left", "Bottom Center", "Bottom Right" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(name.c_str(), &current, items, 9)) {
                    v = static_cast<UIAnchor>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, BodyType>) {
                const char* items[] = { "Static", "Kinematic", "Dynamic" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(name.c_str(), &current, items, 3)) {
                    v = static_cast<BodyType>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, Layer>) {
                const char* items[] = { "Default", "TOP", "BOTTOM" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(name.c_str(), &current, items, 3)) {
                    v = static_cast<Layer>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, CanvasRenderMode>) {
                const char* items[] = { "Screen Space Overlay", "Screen Space Camera", "World Space" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(name.c_str(), &current, items, 3)) {
                    v = static_cast<CanvasRenderMode>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, UILayoutType>) {
                const char* items[] = { "Horizontal", "Vertical" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(name.c_str(), &current, items, 2)) {
                    v = static_cast<UILayoutType>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, TextAlignment>) {
                const char* items[] = { "Left", "Center", "Right" };
                int current = static_cast<int>(v);
                if (ImGui::Combo(name.c_str(), &current, items, 3)) {
                    v = static_cast<TextAlignment>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, EnumFieldValue>) {
                if (!v.Options.empty()) {
                    std::vector<const char*> labels;
                    labels.reserve(v.Options.size());
                    for (const std::string& option : v.Options)
                        labels.push_back(option.c_str());
                    int current = v.Value;
                    if (current < 0)
                        current = 0;
                    if (current >= static_cast<int>(labels.size()))
                        current = static_cast<int>(labels.size()) - 1;
                    if (ImGui::Combo(name.c_str(), &current, labels.data(), static_cast<int>(labels.size()))) {
                        v.Value = current;
                        changed = true;
                    }
                } else {
                    changed = ImGui::DragInt(name.c_str(), &v.Value);
                }
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                int current = static_cast<int>(v);
                if (ImGui::DragInt(name.c_str(), &current, 1.0f, 0, static_cast<int>(AllLayersMask))) {
                    v = static_cast<uint32_t>(current);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, AssetRef>) {
                // Drag-drop target only for this pass -- no
                // inline thumbnail preview (would need
                // ThumbnailCache threaded in here) and no
                // "browse" file dialog, just what dragging from
                // the Content Browser needs.
                ImGui::PushID(name.c_str());
                std::string resolvedPath = v.Guid.empty() ? std::string() : AssetDatabase::ResolvePath(v.Guid);
                std::string filename = resolvedPath.empty() ? "<none>" : std::filesystem::path(resolvedPath).filename().string();
                ImGui::Text("%s", name.c_str());
                ImGui::SameLine(FieldLabelOffset(indentDepth));
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
            } else if constexpr (std::is_same_v<T, std::vector<AssetRef>>) {
                // One row per element, each reusing the single-AssetRef drag-drop-target chrome
                // above almost verbatim (a button showing the resolved filename, drop target for
                // the Content Browser's "ASSET_GUID" payload, a clear button) plus a remove
                // button per row and a trailing add button. Manual add/remove only -- no
                // auto-sizing to match e.g. a sibling Mesh field's real submesh count, since a
                // FieldHandle has no visibility into sibling fields on the same component (see
                // MeshRendererComponent::Materials' own comment).
                ImGui::PushID(name.c_str());
                ImGui::Text("%s", name.c_str());
                int removeIndex = -1;
                for (int i = 0; i < static_cast<int>(v.size()); i++) {
                    ImGui::PushID(i);
                    AssetRef& item = v[i];
                    std::string resolvedPath = item.Guid.empty() ? std::string() : AssetDatabase::ResolvePath(item.Guid);
                    std::string filename = resolvedPath.empty() ? "<none>" : std::filesystem::path(resolvedPath).filename().string();
                    ImGui::Button(filename.c_str(), ImVec2(-64.0f, 0.0f));
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                            item.Guid.assign(static_cast<const char*>(payload->Data));
                            changed = true;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        item.Guid.clear();
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-")) {
                        removeIndex = i;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                if (removeIndex >= 0)
                    v.erase(v.begin() + removeIndex);
                if (ImGui::SmallButton("+ Add")) {
                    v.push_back(AssetRef{});
                    changed = true;
                }
                ImGui::PopID();
            } else if constexpr (std::is_same_v<T, EntityRef>) {
                // Drag-drop target only, same scope as AssetRef above -- accepts the
                // "HIERARCHY_ENTITY" payload HierarchyPanel already drags entities with.
                ImGui::PushID(name.c_str());
                bool valid = v.Handle != EntityRef::Invalid;
                std::string label = "<none>";
                if (valid) {
                    entt::entity handle = static_cast<entt::entity>(v.Handle);
                    if (scene && scene->Registry().valid(handle) && scene->Registry().all_of<NameComponent>(handle))
                        label = scene->Registry().get<NameComponent>(handle).Name;
                    else if (scene)
                        valid = false; // stale handle -- referenced entity no longer exists
                    else
                        label = "<entity #" + std::to_string(v.Handle) + ">"; // no Scene to resolve a name from
                }
                ImGui::Text("%s", name.c_str());
                ImGui::SameLine(FieldLabelOffset(indentDepth));
                ImGui::Button(label.c_str(), ImVec2(valid ? -32.0f : -1.0f, 0.0f));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                        entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
                        v.Handle = static_cast<uint32_t>(dropped);
                        changed = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                if (valid) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        v.Handle = EntityRef::Invalid;
                        changed = true;
                    }
                }
                ImGui::PopID();
            } else if constexpr (std::is_same_v<T, NestedFieldValue>) {
                // Unity's [System.Serializable]-class-as-a-field foldout, for this engine. `v`
                // is a reference into `value`'s own live storage (std::visit's usual guarantee),
                // so mutating the std::any-wrapped values vector in place here -- via the
                // recursive DrawFieldValueWidget calls below -- directly updates `value` itself,
                // no explicit copy-back needed at the end of this branch (unlike every other
                // branch above, which relies on the FieldValue(v) reconstruction DrawFieldWidget
                // does once Set-side -- there's no single scalar `v` to reconstruct here).
                ImGui::PushID(name.c_str());
                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
                bool open = ImGui::TreeNodeEx(name.c_str(), nodeFlags);
                if (open) {
                    ImGui::Indent(kNestedFieldIndent);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
                    ImGui::BeginChild(
                        ImGui::GetID("NestedFields"),
                        ImVec2(0.0f, 0.0f),
                        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AlwaysUseWindowPadding);

                    auto& values = std::any_cast<std::vector<FieldValue>&>(v.Values);
                    auto fieldsFn = std::any_cast<std::vector<FieldHandle> (*)()>(v.FieldsFn);
                    std::vector<FieldHandle> fields = fieldsFn();
                    for (size_t i = 0; i < fields.size() && i < values.size(); i++) {
                        ImGui::PushID(static_cast<int>(i));
                        if (DrawFieldValueWidget(fields[i].Name, values[i], scene, indentDepth + 1))
                            changed = true;
                        ImGui::PopID();
                    }

                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::Unindent(kNestedFieldIndent);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }, value);

        return changed;
    }

}
