#include "DualityEditor/FieldEditorWidget.h"

#include <algorithm>
#include <any>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <entt.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEditor/ThumbnailCache.h"
#include "DualityEditor/Windows/SpriteAssetUI.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Renderer/CanvasRenderMode.h"
#include "DualityEngine/Renderer/TextAlignment.h"
#include "DualityEngine/Renderer/UILayoutType.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        constexpr float kNestedFieldIndent = 14.0f;
        // Unity's mixed-value marker (em dash).
        constexpr const char* kMixed = "\xE2\x80\x94";

        FieldDrawState s_NextState;
        FieldDrawState s_State;
        FieldEditorHooks s_Hooks;
        // Horizontal drag on a numeric field's label scrubs its value (Unity). Set by
        // BeginPropertyRow, consumed by the scalar widget drawn right after it.
        float s_LabelDragDelta = 0.0f;
        std::string s_CurrentTooltip;

        // Never destroyed: its GL textures must not be released after the GL context is gone.
        ThumbnailCache& Thumbnails() {
            static ThumbnailCache* cache = new ThumbnailCache();
            return *cache;
        }

        bool IsMixed(uint32_t bit = 0) { return (s_State.MixedMask & (1u << bit)) != 0; }

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        bool ContainsCaseInsensitive(const std::string& haystack, const char* needle) {
            if (!needle || !needle[0])
                return true;
            return Lower(haystack).find(Lower(needle)) != std::string::npos;
        }

        const char* IconForAssetPath(const std::string& path) {
            const std::string ext = Lower(std::filesystem::path(path).extension().string());
            if (ThumbnailCache::IsImageFile(ext)) return EditorIcons::Image;
            if (ext == ".mat") return EditorIcons::Material;
            if (ext == ".prefab" || ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") return EditorIcons::Cube;
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") return EditorIcons::Music;
            if (ext == ".scene") return EditorIcons::Map;
            if (ext == ".ts" || ext == ".h" || ext == ".cpp") return EditorIcons::Code;
            return EditorIcons::File;
        }

        // Every project asset (file with a .meta), for the object picker.
        struct PickerEntry { std::string Guid; std::string Path; std::string Name; };
        std::vector<PickerEntry> CollectProjectAssets() {
            std::vector<PickerEntry> entries;
            auto project = Project::GetActive();
            if (!project)
                return entries;
            std::error_code error;
            for (auto it = std::filesystem::recursive_directory_iterator(project->GetAssetsDirectory(), error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                if (!it->is_regular_file() || it->path().extension() == ".meta")
                    continue;
                std::filesystem::path meta = it->path();
                meta += ".meta";
                if (!std::filesystem::exists(meta))
                    continue;
                const std::string path = it->path().generic_string();
                const std::string guid = AssetMeta::EnsureMetaFile(it->path());
                entries.push_back({ guid, path, it->path().filename().string() });
                // Sliced sprites are pickable on their own (Unity's sprite picker lists them).
                if (ThumbnailCache::IsImageFile(it->path().extension().string()))
                    for (const SpriteAssetUI::SpriteEntry& sprite : SpriteAssetUI::ListSprites(it->path().string(), guid))
                        entries.push_back({ sprite.Reference, path, sprite.Name });
            }
            std::sort(entries.begin(), entries.end(), [](const PickerEntry& a, const PickerEntry& b) { return a.Name < b.Name; });
            return entries;
        }

        // Unity's object field: a recessed frame holding an icon (or a small thumbnail) and the
        // referenced object's name, plus a round picker button at the right edge.
        // Returns: 0 nothing, 1 clicked, 2 double-clicked, 3 picker clicked.
        int ObjectField(const char* id, const std::string& text, const char* icon, uint32_t thumbnail, bool missing, bool empty,
            const ImVec4& thumbnailUV = ImVec4(0.0f, 0.0f, 1.0f, 1.0f)) {
            const float height = ImGui::GetFrameHeight();
            const float width = ImGui::CalcItemWidth();
            const float pickerWidth = height;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::PushID(id);
            ImGui::InvisibleButton("##object", ImVec2(std::max(10.0f, width - pickerWidth), height));
            int result = 0;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                result = 2;
            else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                result = 1;
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max(pos.x + width, pos.y + height);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
            drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().FrameRounding);
            float x = min.x + 4.0f;
            const ImU32 textColor = missing ? EditorColors::PrefabMissing() : ImGui::GetColorU32(empty ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            if (thumbnail) {
                const float size = height - 4.0f;
                drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(thumbnail)), ImVec2(x, min.y + 2.0f), ImVec2(x + size, min.y + 2.0f + size),
                    ImVec2(thumbnailUV.x, thumbnailUV.y), ImVec2(thumbnailUV.z, thumbnailUV.w));
                x += size + 4.0f;
            } else if (icon && !empty) {
                drawList->AddText(ImVec2(x, min.y + ImGui::GetStyle().FramePadding.y), textColor, icon);
                x += ImGui::CalcTextSize(icon).x + 5.0f;
            }
            // RenderTextEllipsis draws with the current text color.
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::RenderTextEllipsis(drawList, ImVec2(x, min.y + ImGui::GetStyle().FramePadding.y),
                ImVec2(max.x - pickerWidth - 2.0f, max.y), max.x - pickerWidth - 2.0f, max.x - pickerWidth, text.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            if (ImGui::Button(EditorIcons::Crosshairs, ImVec2(pickerWidth, height)))
                result = 3;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopID();
            return result;
        }

        bool ColorField(const char* id, float* rgba, bool hasAlpha) {
            bool changed = false;
            const float width = ImGui::CalcItemWidth();
            const float hexWidth = std::min(width * 0.45f, ImGui::CalcTextSize("#FFFFFFFF").x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f);
            ImGui::PushID(id);
            const ImVec4 color(rgba[0], rgba[1], rgba[2], hasAlpha ? rgba[3] : 1.0f);
            if (IsMixed()) {
                if (ImGui::Button(kMixed, ImVec2(width - hexWidth - ImGui::GetStyle().ItemInnerSpacing.x, 0.0f)))
                    ImGui::OpenPopup("picker");
            } else if (ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
                           ImVec2(width - hexWidth - ImGui::GetStyle().ItemInnerSpacing.x, ImGui::GetFrameHeight()))) {
                ImGui::OpenPopup("picker");
            }
            if (ImGui::BeginPopup("picker")) {
                ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_AlphaBar;
                if (!hasAlpha)
                    flags |= ImGuiColorEditFlags_NoAlpha;
                changed |= hasAlpha ? ImGui::ColorPicker4("##picker", rgba, flags) : ImGui::ColorPicker3("##picker", rgba, flags);
                ImGui::EndPopup();
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(hexWidth);
            char hex[16];
            const int r = static_cast<int>(std::clamp(rgba[0], 0.0f, 1.0f) * 255.0f + 0.5f);
            const int g = static_cast<int>(std::clamp(rgba[1], 0.0f, 1.0f) * 255.0f + 0.5f);
            const int b = static_cast<int>(std::clamp(rgba[2], 0.0f, 1.0f) * 255.0f + 0.5f);
            const int a = hasAlpha ? static_cast<int>(std::clamp(rgba[3], 0.0f, 1.0f) * 255.0f + 0.5f) : 255;
            if (IsMixed())
                std::snprintf(hex, sizeof(hex), "%s", "");
            else if (hasAlpha)
                std::snprintf(hex, sizeof(hex), "%02X%02X%02X%02X", r, g, b, a);
            else
                std::snprintf(hex, sizeof(hex), "%02X%02X%02X", r, g, b);
            if (ImGui::InputTextWithHint("##hex", "#hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsUppercase)) {
                unsigned int value = 0;
                const size_t length = std::char_traits<char>::length(hex);
                if ((length == 6 || length == 8) && std::sscanf(hex, "%x", &value) == 1) {
                    if (length == 6)
                        value = (value << 8) | 0xFF;
                    rgba[0] = ((value >> 24) & 0xFF) / 255.0f;
                    rgba[1] = ((value >> 16) & 0xFF) / 255.0f;
                    rgba[2] = ((value >> 8) & 0xFF) / 255.0f;
                    if (hasAlpha)
                        rgba[3] = (value & 0xFF) / 255.0f;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Hex color (RRGGBB%s) -- Enter to apply", hasAlpha ? "AA" : "");
            ImGui::PopID();
            return changed;
        }

        // Unity's vector field: "X [  ] Y [  ] Z [  ]" sharing the row. Returns true if any
        // component changed.
        bool VectorField(const char* id, float* values, int count, float speed, float minValue = 0.0f, float maxValue = 0.0f) {
            static const char* labels[] = { "X", "Y", "Z", "W" };
            bool changed = false;
            const ImGuiStyle& style = ImGui::GetStyle();
            const float width = ImGui::CalcItemWidth();
            const float spacing = style.ItemInnerSpacing.x * 2.0f;
            const float letterWidth = ImGui::CalcTextSize("X").x + style.ItemInnerSpacing.x;
            const float fieldWidth = std::max(20.0f, (width - spacing * (count - 1)) / count - letterWidth);
            ImGui::PushID(id);
            ImGui::BeginGroup();
            for (int i = 0; i < count; ++i) {
                if (i > 0)
                    ImGui::SameLine(0.0f, spacing);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(labels[i]);
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                ImGui::SetNextItemWidth(fieldWidth);
                ImGui::PushID(i);
                changed |= ImGui::DragFloat("##c", &values[i], speed, minValue, maxValue, IsMixed(static_cast<uint32_t>(i)) ? kMixed : "%g");
                ImGui::PopID();
            }
            ImGui::EndGroup();
            ImGui::PopID();
            if (s_LabelDragDelta != 0.0f) {
                for (int i = 0; i < count; ++i)
                    values[i] += s_LabelDragDelta * speed;
                changed = true;
            }
            return changed;
        }

        template<typename TEnum, size_t N>
        bool EnumCombo(const char* id, TEnum& value, const char* const (&items)[N]) {
            int current = static_cast<int>(value);
            if (current < 0 || current >= static_cast<int>(N))
                current = 0;
            bool changed = false;
            if (ImGui::BeginCombo(id, IsMixed() ? kMixed : items[current])) {
                for (int i = 0; i < static_cast<int>(N); ++i) {
                    if (ImGui::Selectable(items[i], i == current)) {
                        value = static_cast<TEnum>(i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool LayerMaskField(const char* id, uint32_t& mask) {
            const char* names[] = { "Default", "TOP", "BOTTOM" };
            std::string preview;
            if (IsMixed())
                preview = kMixed;
            else if (mask == 0)
                preview = "Nothing";
            else if (mask == AllLayersMask)
                preview = "Everything";
            else {
                int count = 0;
                for (int i = 0; i < 3; ++i) {
                    if (mask & LayerBit(i)) {
                        preview += (count++ ? ", " : "") + std::string(names[i]);
                    }
                }
                if (mask & ~0x7u)
                    preview = count ? "Mixed..." : "Other layers";
            }
            bool changed = false;
            if (ImGui::BeginCombo(id, preview.c_str())) {
                if (ImGui::Selectable("Nothing", mask == 0)) { mask = 0; changed = true; }
                if (ImGui::Selectable("Everything", mask == AllLayersMask)) { mask = AllLayersMask; changed = true; }
                ImGui::Separator();
                for (int i = 0; i < 3; ++i) {
                    bool on = (mask & LayerBit(i)) != 0;
                    if (ImGui::Checkbox(names[i], &on)) {
                        mask = on ? (mask | LayerBit(i)) : (mask & ~LayerBit(i));
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        std::string EntityLabel(Scene* scene, uint32_t handle, bool& valid) {
            valid = handle != EntityRef::Invalid;
            if (!valid)
                return "None (Entity)";
            const entt::entity entity = static_cast<entt::entity>(handle);
            if (scene && scene->Registry().valid(entity) && scene->Registry().all_of<NameComponent>(entity))
                return scene->Registry().get<NameComponent>(entity).Name;
            if (scene) {
                valid = false;
                return "Missing (Entity)";
            }
            return "<entity #" + std::to_string(handle) + ">";
        }

        // Object field for an entity reference. Returns true when `handle` changed.
        bool EntityField(const char* id, uint32_t& handle, Scene* scene) {
            bool changed = false;
            bool valid = false;
            const std::string label = EntityLabel(scene, handle, valid);
            const bool missing = handle != EntityRef::Invalid && !valid;
            const int action = ObjectField(id, IsMixed() ? std::string(kMixed) : label, EditorIcons::Cubes, 0, missing, !valid && !missing);
            if (action == 1 && valid && s_Hooks.PingEntity)
                s_Hooks.PingEntity(handle);
            if (action == 2 && valid && s_Hooks.SelectEntity)
                s_Hooks.SelectEntity(handle);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                    handle = static_cast<uint32_t>(*static_cast<const entt::entity*>(payload->Data));
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PushID(id);
            if (action == 3)
                ImGui::OpenPopup("EntityPicker");
            if (ImGui::BeginPopup("EntityPicker")) {
                static char filter[64] = "";
                if (ImGui::IsWindowAppearing()) {
                    filter[0] = '\0';
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(260.0f);
                ImGui::InputTextWithHint("##filter", "Search entities", filter, sizeof(filter));
                ImGui::BeginChild("##list", ImVec2(260.0f, 280.0f));
                if (ImGui::Selectable("None", handle == EntityRef::Invalid)) {
                    handle = EntityRef::Invalid;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (scene) {
                    for (Entity entity : scene->GetHierarchyTraversalOrder()) {
                        const std::string& name = entity.GetComponent<NameComponent>().Name;
                        if (!ContainsCaseInsensitive(name, filter))
                            continue;
                        const uint32_t value = static_cast<uint32_t>(entity.Handle());
                        ImGui::PushID(static_cast<int>(value));
                        if (ImGui::Selectable((std::string(EditorIcons::Cubes) + "  " + name).c_str(), value == handle)) {
                            handle = value;
                            changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupContextItem("EntityFieldContext")) {
                if (ImGui::MenuItem("Clear", nullptr, false, handle != EntityRef::Invalid)) {
                    handle = EntityRef::Invalid;
                    changed = true;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            return changed;
        }

        bool IsMeshFile(const std::string& path) {
            const std::string ext = Lower(std::filesystem::path(path).extension().string());
            return ext == ".obj" || ext == ".dmesh" || ext == ".mesh" || ext == ".fbx" || ext == ".gltf" || ext == ".glb";
        }

        uint32_t BuiltinMeshThumbnail(MeshPrimitive primitive) {
            return s_Hooks.BuiltinMeshThumbnail ? s_Hooks.BuiltinMeshThumbnail(primitive) : 0;
        }

        // The Mesh picker's "Built-in" section (Unity lists its default Cube/Sphere/... meshes
        // first): one thumbnail row per mesh; each row can also be dragged onto Mesh fields
        // and the Scene view. Returns true when `guid` changed.
        bool BuiltinMeshPickerRows(std::string& guid, const char* filter) {
            bool changed = false;
            const float rowHeight = ImGui::GetFrameHeight() * 1.6f;
            for (const BuiltinMeshes::Info& mesh : BuiltinMeshes::All) {
                if (!ContainsCaseInsensitive(mesh.Name, filter))
                    continue;
                const std::string reference = BuiltinMeshes::ReferenceFor(mesh.Primitive);
                ImGui::PushID(mesh.Name);
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##builtin", reference == guid, 0, ImVec2(0.0f, rowHeight))) {
                    guid = reference;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("ASSET_GUID", reference.c_str(), reference.size() + 1);
                    ImGui::Text("%s  %s", EditorIcons::Cube, mesh.Name);
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Built-in mesh (%s)", reference.c_str());
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const float size = rowHeight - 4.0f;
                if (const uint32_t texture = BuiltinMeshThumbnail(mesh.Primitive))
                    drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture)),
                        ImVec2(rowStart.x + 2.0f, rowStart.y + 2.0f), ImVec2(rowStart.x + 2.0f + size, rowStart.y + 2.0f + size), ImVec2(0, 1), ImVec2(1, 0));
                drawList->AddText(ImVec2(rowStart.x + size + 10.0f, rowStart.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), mesh.Name);
                ImGui::PopID();
            }
            return changed;
        }

        // The asset kind of the field currently being drawn (FieldHandle::AssetType).
        std::string s_CurrentAssetType;

        // Object field for an asset reference. Returns true when `guid` changed.
        bool AssetField(const char* id, std::string& guid) {
            bool changed = false;
            const bool meshField = s_CurrentAssetType == "Mesh";
            MeshPrimitive builtinMesh = MeshPrimitive::Cube;
            const bool isBuiltinMesh = BuiltinMeshes::TryGetPrimitive(guid, builtinMesh);
            const std::string path = (guid.empty() || isBuiltinMesh) ? std::string() : AssetDatabase::ResolvePath(guid);
            const bool missing = !guid.empty() && path.empty() && !isBuiltinMesh;
            std::string label = guid.empty() ? std::string(meshField ? "None (Mesh)" : "None (Asset)") : (missing ? "Missing (" + guid.substr(0, 8) + ")" : std::filesystem::path(path).filename().string());
            uint32_t thumbnail = 0;
            ImVec4 thumbnailUV(0.0f, 0.0f, 1.0f, 1.0f);
            if (isBuiltinMesh) {
                label = BuiltinMeshes::NameOf(builtinMesh);
                thumbnail = BuiltinMeshThumbnail(builtinMesh);
                thumbnailUV = ImVec4(0.0f, 1.0f, 1.0f, 0.0f); // framebuffer rows are bottom-up
            } else if (!path.empty() && ThumbnailCache::IsImageFile(Lower(std::filesystem::path(path).extension().string()))) {
                thumbnail = Thumbnails().GetThumbnail(path);
                // A sliced sprite shows its own name and sub-rect (Unity's sprite object field).
                ResolvedSprite sprite;
                if (SpriteRef::IsSubSprite(guid) && SpriteDatabase::Resolve(guid, sprite)) {
                    label = sprite.IsSubSprite() ? sprite.Name : label + " (missing sprite)";
                    thumbnailUV = ImVec4(sprite.UV.x, sprite.UV.y, sprite.UV.z, sprite.UV.w);
                }
            }
            const int action = ObjectField(id, IsMixed() ? std::string(kMixed) : label, isBuiltinMesh ? EditorIcons::Cube : (path.empty() ? nullptr : IconForAssetPath(path)), IsMixed() ? 0 : thumbnail, missing, guid.empty(), thumbnailUV);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && isBuiltinMesh)
                ImGui::SetTooltip("Built-in mesh (%s)", guid.c_str());
            else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !path.empty())
                ImGui::SetTooltip("%s", path.c_str());
            if (action == 1 && !path.empty() && s_Hooks.PingAsset)
                s_Hooks.PingAsset(path);
            if (action == 2 && !path.empty() && s_Hooks.SelectAsset)
                s_Hooks.SelectAsset(path);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                    guid.assign(static_cast<const char*>(payload->Data));
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PushID(id);
            static std::vector<PickerEntry> s_PickerEntries;
            if (action == 3) {
                s_PickerEntries = CollectProjectAssets();
                ImGui::OpenPopup("AssetPicker");
            }
            if (ImGui::BeginPopup("AssetPicker")) {
                static char filter[64] = "";
                static bool sameType = true;
                if (ImGui::IsWindowAppearing()) {
                    filter[0] = '\0';
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(300.0f);
                ImGui::InputTextWithHint("##filter", "Search assets", filter, sizeof(filter));
                // A typed field (Mesh) lists only that kind, like Unity's object picker; an
                // untyped one can narrow to the current asset's extension.
                const std::string currentExtension = (meshField || path.empty()) ? std::string() : Lower(std::filesystem::path(path).extension().string());
                if (!currentExtension.empty()) {
                    ImGui::SameLine();
                    ImGui::Checkbox(("Only " + currentExtension).c_str(), &sameType);
                }
                ImGui::BeginChild("##list", ImVec2(420.0f, 320.0f));
                if (ImGui::Selectable("None", guid.empty())) {
                    guid.clear();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (meshField) {
                    ImGui::SeparatorText("Built-in");
                    changed |= BuiltinMeshPickerRows(guid, filter);
                    ImGui::SeparatorText("Assets");
                }
                for (const PickerEntry& entry : s_PickerEntries) {
                    if (!ContainsCaseInsensitive(entry.Name, filter))
                        continue;
                    if (meshField && !IsMeshFile(entry.Path))
                        continue;
                    if (sameType && !currentExtension.empty() && Lower(std::filesystem::path(entry.Path).extension().string()) != currentExtension)
                        continue;
                    ImGui::PushID(entry.Guid.c_str());
                    if (ImGui::Selectable((std::string(IconForAssetPath(entry.Path)) + "  " + entry.Name).c_str(), entry.Guid == guid)) {
                        guid = entry.Guid;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("%s", entry.Path.c_str());
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupContextItem("AssetFieldContext")) {
                if (ImGui::MenuItem("Clear", nullptr, false, !guid.empty())) {
                    guid.clear();
                    changed = true;
                }
                if (ImGui::MenuItem("Select Asset", nullptr, false, !path.empty()) && s_Hooks.SelectAsset)
                    s_Hooks.SelectAsset(path);
                ImGui::EndPopup();
            }
            ImGui::PopID();
            return changed;
        }

        // A Unity list header: foldout "Name" + element count on the right.
        bool ListHeader(const std::string& name, int count, int indentDepth) {
            (void)indentDepth;
            const bool open = ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 40.0f);
            ImGui::TextDisabled("%d", count);
            return open;
        }

        bool ListFooter(bool canAdd, bool& add, int selectedRemove, bool& remove) {
            const float buttonWidth = ImGui::GetFrameHeight() * 1.2f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonWidth * 2.0f - ImGui::GetStyle().ItemSpacing.x);
            ImGui::BeginDisabled(!canAdd);
            add = ImGui::Button(EditorIcons::Add, ImVec2(buttonWidth, 0.0f));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(selectedRemove < 0);
            remove = ImGui::Button("-", ImVec2(buttonWidth, 0.0f));
            ImGui::EndDisabled();
            return add || remove;
        }
    }

    void SetNextFieldDrawState(FieldDrawState state) {
        s_NextState = std::move(state);
    }

    void SetFieldEditorHooks(FieldEditorHooks hooks) {
        s_Hooks = std::move(hooks);
    }

    float FieldLabelWidth() {
        return std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 90.0f, 260.0f);
    }

    std::string BeginPropertyRow(const std::string& label, int indentDepth) {
        (void)indentDepth;
        const float labelWidth = FieldLabelWidth();
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(label.c_str());
        ImGui::InvisibleButton("##label", ImVec2(labelWidth, height));
        const bool hovered = ImGui::IsItemHovered();
        s_LabelDragDelta = (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) ? ImGui::GetIO().MouseDelta.x : 0.0f;
        if (hovered || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (hovered && !s_CurrentTooltip.empty())
            ImGui::SetTooltip("%s", s_CurrentTooltip.c_str());
        if (s_State.ContextMenu && ImGui::BeginPopupContextItem("##labelmenu")) {
            s_State.ContextMenu();
            ImGui::EndPopup();
        }
        ImGui::PopID();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (s_State.Overridden) {
            // Unity's blue prefab-override bar in the left margin.
            const float x = ImGui::GetWindowPos().x + 2.0f;
            drawList->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + 2.0f, pos.y + height), EditorColors::PrefabText());
            ImGui::PushFont(EditorBoldFont());
        }
        const float textY = pos.y + ImGui::GetStyle().FramePadding.y;
        ImGui::RenderTextEllipsis(drawList, ImVec2(pos.x, textY), ImVec2(pos.x + labelWidth - 4.0f, pos.y + height),
            pos.x + labelWidth - 4.0f, pos.x + labelWidth, label.c_str(), nullptr, nullptr);
        if (s_State.Overridden)
            ImGui::PopFont();

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetNextItemWidth(-1.0f);
        return "##" + label;
    }

    bool DrawFieldWidget(const FieldHandle& field, void* instance, Scene* scene) {
        // Optional presentation metadata (Unity's [Header]/[Range]/[Tooltip]) -- see
        // FieldHandle's own comment.
        if (!field.Header.empty()) {
            ImGui::Spacing();
            ImGui::PushFont(EditorBoldFont());
            ImGui::TextUnformatted(field.Header.c_str());
            ImGui::PopFont();
        }
        FieldValue value = field.Get(instance);
        bool changed = false;
        s_CurrentTooltip = field.Tooltip;
        s_CurrentAssetType = field.AssetType;
        if (field.HasRange && (std::holds_alternative<float>(value) || std::holds_alternative<int>(value))) {
            s_State = std::move(s_NextState);
            s_NextState = {};
            const std::string id = BeginPropertyRow(field.Name);
            if (auto* f = std::get_if<float>(&value))
                changed = ImGui::SliderFloat(id.c_str(), f, field.RangeMin, field.RangeMax, IsMixed() ? kMixed : "%.3f");
            else if (auto* i = std::get_if<int>(&value))
                changed = ImGui::SliderInt(id.c_str(), i, static_cast<int>(field.RangeMin), static_cast<int>(field.RangeMax), IsMixed() ? kMixed : "%d");
            s_State = {};
        } else {
            changed = DrawFieldValueWidget(field.Name, value, scene);
        }
        s_CurrentTooltip.clear();
        s_CurrentAssetType.clear();
        if (changed)
            field.Set(instance, value);
        return changed;
    }

    bool DrawFieldValueWidget(const std::string& name, FieldValue& value, Scene* scene, int indentDepth) {
        bool changed = false;
        // The draw state applies to the top-level field only (not to a nested struct's members).
        const bool topLevel = indentDepth == 0;
        if (topLevel) {
            s_State = std::move(s_NextState);
            s_NextState = {};
        }
        ImGui::PushID(name.c_str());

        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, glm::vec3>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                // RGB fields (light colors) get a color field; everything else is a vector.
                if (name == "Color" || (name.size() > 6 && name.compare(name.size() - 6, 6, " Color") == 0))
                    changed = ColorField(id.c_str(), &v.x, false);
                else
                    changed = VectorField(id.c_str(), &v.x, 3, name == "Scale" ? 0.01f : 0.05f);
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = VectorField(id.c_str(), &v.x, 2, 0.05f);
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = VectorField(id.c_str(), &v.x, 4, 0.01f);
            } else if constexpr (std::is_same_v<T, Color4>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = ColorField(id.c_str(), &v.Value.x, true);
            } else if constexpr (std::is_same_v<T, float>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = ImGui::DragFloat(id.c_str(), &v, 0.05f, 0.0f, 0.0f, IsMixed() ? kMixed : "%g");
                if (s_LabelDragDelta != 0.0f) {
                    v += s_LabelDragDelta * 0.05f;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, int>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = ImGui::DragInt(id.c_str(), &v, 0.2f, 0, 0, IsMixed() ? kMixed : "%d");
                if (s_LabelDragDelta != 0.0f) {
                    v += static_cast<int>(s_LabelDragDelta);
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, bool>) {
                BeginPropertyRow(name, indentDepth);
                if (IsMixed())
                    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                changed = ImGui::Checkbox(("##" + name).c_str(), &v);
                if (IsMixed())
                    ImGui::PopItemFlag();
            } else if constexpr (std::is_same_v<T, std::string>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "%s", IsMixed() ? "" : v.c_str());
                if (ImGui::InputTextWithHint(id.c_str(), IsMixed() ? kMixed : "", buffer, sizeof(buffer))) {
                    v = buffer;
                    changed = true;
                }
            } else if constexpr (std::is_same_v<T, Screen>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Top", "Bottom" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, ProjectionType>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Orthographic", "Perspective" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, MeshPrimitive>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Cube", "Sphere", "Plane", "Capsule", "Cylinder", "Quad" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, UIAnchor>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Top Left", "Top Center", "Top Right", "Middle Left", "Middle Center", "Middle Right", "Bottom Left", "Bottom Center", "Bottom Right" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, BodyType>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Static", "Kinematic", "Dynamic" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, Layer>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Default", "TOP", "BOTTOM" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, CanvasRenderMode>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Screen Space Overlay", "Screen Space Camera", "World Space" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, UILayoutType>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Horizontal", "Vertical" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, TextAlignment>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                static const char* const items[] = { "Left", "Center", "Right" };
                changed = EnumCombo(id.c_str(), v, items);
            } else if constexpr (std::is_same_v<T, EnumFieldValue>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                if (!v.Options.empty()) {
                    const int current = std::clamp(v.Value, 0, static_cast<int>(v.Options.size()) - 1);
                    if (ImGui::BeginCombo(id.c_str(), IsMixed() ? kMixed : v.Options[static_cast<size_t>(current)].c_str())) {
                        for (int i = 0; i < static_cast<int>(v.Options.size()); ++i) {
                            if (ImGui::Selectable(v.Options[static_cast<size_t>(i)].c_str(), i == current)) {
                                v.Value = i;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    changed = ImGui::DragInt(id.c_str(), &v.Value);
                }
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = LayerMaskField(id.c_str(), v);
            } else if constexpr (std::is_same_v<T, AssetRef>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = AssetField(id.c_str(), v.Guid);
            } else if constexpr (std::is_same_v<T, EntityRef>) {
                const std::string id = BeginPropertyRow(name, indentDepth);
                changed = EntityField(id.c_str(), v.Handle, scene);
            } else if constexpr (std::is_same_v<T, std::vector<AssetRef>>) {
                // Unity list: foldout + "Element N" object fields; drop assets on the list to append.
                static int s_SelectedElement = -1;
                if (ListHeader(name, static_cast<int>(v.size()), indentDepth)) {
                    int removeIndex = -1;
                    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
                        ImGui::PushID(i);
                        const std::string id = BeginPropertyRow("Element " + std::to_string(i), indentDepth + 1);
                        if (AssetField(id.c_str(), v[static_cast<size_t>(i)].Guid))
                            changed = true;
                        if (ImGui::IsItemClicked() || ImGui::IsItemActive())
                            s_SelectedElement = i;
                        ImGui::PopID();
                    }
                    const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
                    if (activePayload && activePayload->IsDataType("ASSET_GUID")) {
                        ImGui::Button("Drop here to add", ImVec2(-1.0f, 0.0f));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                                v.push_back(AssetRef{ static_cast<const char*>(payload->Data) });
                                changed = true;
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                    bool add = false, remove = false;
                    const int selected = v.empty() ? -1 : std::clamp(s_SelectedElement, 0, static_cast<int>(v.size()) - 1);
                    ListFooter(true, add, selected, remove);
                    if (add) {
                        v.push_back(v.empty() ? AssetRef{} : v.back());
                        changed = true;
                    }
                    if (remove && selected >= 0)
                        removeIndex = selected;
                    if (removeIndex >= 0) {
                        v.erase(v.begin() + removeIndex);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
            } else if constexpr (std::is_same_v<T, std::vector<EntityRef>>) {
                // Ordered entity references (paths / splines): rows can be reordered.
                static int s_SelectedElement = -1;
                if (ListHeader(name, static_cast<int>(v.size()), indentDepth)) {
                    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
                        ImGui::PushID(i);
                        const std::string id = BeginPropertyRow("Element " + std::to_string(i), indentDepth + 1);
                        if (EntityField(id.c_str(), v[static_cast<size_t>(i)].Handle, scene))
                            changed = true;
                        if (ImGui::IsItemClicked())
                            s_SelectedElement = i;
                        ImGui::PopID();
                    }
                    const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
                    if (activePayload && activePayload->IsDataType("HIERARCHY_ENTITY")) {
                        ImGui::Button("Drop here to add", ImVec2(-1.0f, 0.0f));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                                v.push_back(EntityRef{ static_cast<uint32_t>(*static_cast<const entt::entity*>(payload->Data)) });
                                changed = true;
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }
                    const int selected = v.empty() ? -1 : std::clamp(s_SelectedElement, 0, static_cast<int>(v.size()) - 1);
                    // Move the selected element up / down (path order matters).
                    ImGui::BeginDisabled(selected <= 0);
                    if (ImGui::SmallButton("Move Up")) {
                        std::swap(v[static_cast<size_t>(selected)], v[static_cast<size_t>(selected - 1)]);
                        s_SelectedElement = selected - 1;
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(selected < 0 || selected + 1 >= static_cast<int>(v.size()));
                    if (ImGui::SmallButton("Move Down")) {
                        std::swap(v[static_cast<size_t>(selected)], v[static_cast<size_t>(selected + 1)]);
                        s_SelectedElement = selected + 1;
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    bool add = false, remove = false;
                    ListFooter(true, add, selected, remove);
                    if (add) {
                        v.push_back(EntityRef{});
                        changed = true;
                    }
                    if (remove && selected >= 0) {
                        v.erase(v.begin() + selected);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
            } else if constexpr (std::is_same_v<T, NestedFieldValue>) {
                // Unity's [System.Serializable]-class-as-a-field foldout. `v` is a reference into
                // `value`'s own storage, so the recursive edits below update `value` directly.
                if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding)) {
                    auto& values = std::any_cast<std::vector<FieldValue>&>(v.Values);
                    auto fieldsFn = std::any_cast<std::vector<FieldHandle> (*)()>(v.FieldsFn);
                    std::vector<FieldHandle> fields = fieldsFn();
                    for (size_t i = 0; i < fields.size() && i < values.size(); i++) {
                        ImGui::PushID(static_cast<int>(i));
                        if (DrawFieldValueWidget(fields[i].Name, values[i], scene, indentDepth + 1))
                            changed = true;
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
        }, value);

        ImGui::PopID();
        if (topLevel)
            s_State = {};
        s_LabelDragDelta = 0.0f;
        return changed;
    }

}
