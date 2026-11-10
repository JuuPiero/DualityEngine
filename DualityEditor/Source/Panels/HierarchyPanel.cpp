#include "DualityEditor/Panels/HierarchyPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/Windows/SpriteAssetUI.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/ModelAsset.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Scene/PrefabSerializer.h"

namespace Duality {

    namespace {
        const char* IconForEntity(Entity entity) {
            if (entity.HasComponent<PrefabInstanceComponent>())
                return EditorIcons::Cube;
            if (entity.HasComponent<CameraComponent>())
                return EditorIcons::Camera;
            if (entity.HasComponent<DirectionalLightComponent>() || entity.HasComponent<PointLightComponent>() || entity.HasComponent<SpotLightComponent>())
                return EditorIcons::Light;
            if (entity.HasComponent<CanvasComponent>())
                return EditorIcons::Square;
            if (entity.HasComponent<UITextComponent>())
                return EditorIcons::Font;
            if (entity.HasComponent<SpriteRendererComponent>() || entity.HasComponent<UIImageComponent>())
                return EditorIcons::Image;
            if (entity.HasComponent<MeshRendererComponent>() || entity.HasComponent<SkinnedMeshRendererComponent>())
                return EditorIcons::Cube;
            if (entity.HasComponent<AudioSourceComponent>())
                return EditorIcons::Audio;
            if (entity.HasComponent<BehaviourComponent>())
                return EditorIcons::Code;
            return EditorIcons::Cubes;
        }

        bool IsLive(EditorContext& ctx, Entity entity) {
            return entity && entity.GetScene() == &ctx.SceneRef && ctx.SceneRef.Registry().valid(entity.Handle());
        }

        Entity DraggedHierarchyEntity(EditorContext& ctx) {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (!payload || std::strcmp(payload->DataType, "HIERARCHY_ENTITY") != 0 ||
                payload->DataSize != sizeof(entt::entity))
                return {};
            entt::entity handle = *static_cast<const entt::entity*>(payload->Data);
            return ctx.SceneRef.Registry().valid(handle) ? Entity(handle, &ctx.SceneRef) : Entity{};
        }

        bool IsDescendantOrSelf(Entity entity, Entity ancestor) {
            for (Entity current = entity; current; current = current.GetComponent<HierarchyComponent>().Parent) {
                if (current == ancestor)
                    return true;
            }
            return false;
        }

        // The entities a drag moves: the whole top-level selection when the dragged row is part
        // of it (Unity drags every selected object), else just the dragged row.
        std::vector<Entity> MovedEntities(EditorContext& ctx, Entity dragged) {
            if (EditorCommands::IsSelected(ctx, dragged))
                return EditorCommands::TopLevelSelection(ctx);
            return { dragged };
        }

        bool CanMoveUnder(const std::vector<Entity>& moved, Entity parent) {
            for (Entity entity : moved) {
                if (parent && IsDescendantOrSelf(parent, entity))
                    return false;
            }
            return !moved.empty();
        }

        const std::vector<Entity>& SiblingsFor(Scene& scene, Entity parent) {
            return parent ? parent.GetComponent<HierarchyComponent>().Children : scene.GetRootEntities();
        }

        // Moves `moved` (in order) under `parent` so they start at `reference`'s position
        // (before it, or after it), or at `fallbackIndex` when `reference` is empty.
        void MoveEntities(EditorContext& ctx, const std::vector<Entity>& moved, Entity parent, Entity reference, bool after, size_t fallbackIndex) {
            if (!CanMoveUnder(moved, parent))
                return;
            // 1) Park every moved entity at the end of the destination list...
            for (Entity entity : moved)
                ctx.SceneRef.SetSiblingIndex(entity, parent, SIZE_MAX, /*preserveWorldPosition=*/true);
            // 2) ...then the untouched siblings form the list prefix, so the insertion index is
            //    measured among them and each moved entity is inserted one slot after the other.
            const std::vector<Entity>& siblings = SiblingsFor(ctx.SceneRef, parent);
            size_t insertAt = std::min(fallbackIndex, siblings.size() - moved.size());
            if (reference) {
                auto found = std::find(siblings.begin(), siblings.end(), reference);
                if (found != siblings.end())
                    insertAt = static_cast<size_t>(found - siblings.begin()) + (after ? 1 : 0);
            }
            for (size_t i = 0; i < moved.size(); ++i)
                ctx.SceneRef.SetSiblingIndex(moved[i], parent, insertAt + i, /*preserveWorldPosition=*/true);
            MarkSceneDirty(ctx, moved.size() == 1 ? "Reparent" : "Reparent Objects");
        }

        bool InstantiateDroppedPrefab(EditorContext& ctx, const ImGuiPayload* payload, Entity parent) {
            std::string guid = static_cast<const char*>(payload->Data);
            std::string path = AssetDatabase::ResolvePath(guid);
            if (!path.empty() && SpriteAssetUI::IsSpriteReference(guid)) {
                // Unity: dropping a sprite (or texture) into the Hierarchy creates a GameObject
                // with a SpriteRenderer at its native size.
                const std::string spriteName = SpriteAssetUI::DisplayName(guid);
                Entity sprite = ctx.SceneRef.CreateEntity(spriteName.empty() ? std::string("Sprite") : std::filesystem::path(spriteName).stem().string());
                SpriteRendererComponent& renderer = sprite.AddComponent<SpriteRendererComponent>();
                renderer.Texture.Guid = guid;
                glm::vec2 nativeSize;
                if (SpriteAssetUI::NativeWorldSize(guid, nativeSize))
                    renderer.Size = nativeSize;
                ctx.SceneRef.SetParent(sprite, IsLive(ctx, parent) ? parent : ctx.SceneRef.EnsureScreenRoot(Screen::Top), {}, false);
                EditorCommands::SelectOnly(ctx, sprite);
                ctx.Session.RequestRevealSelection = true;
                MarkSceneDirty(ctx, "Create Sprite");
                return true;
            }
            // A .prefab, or an imported model's generated prefab (linked to the model GUID).
            const std::string prefabPath = path.empty() ? std::string() : ModelAsset::ResolveInstantiablePrefab(guid);
            if (prefabPath.empty())
                return false;
            Entity instance = PrefabSerializer::Instantiate(ctx.SceneRef, prefabPath, IsLive(ctx, parent) ? parent : Entity{}, AssetRef{ guid });
            if (!instance)
                return false;
            EditorCommands::SelectOnly(ctx, instance);
            ctx.Session.RequestRevealSelection = true;
            MarkSceneDirty(ctx, "Instantiate Prefab");
            return true;
        }

        // Prefab instance root above (or at) `entity`, if any -- whole instance subtrees are
        // drawn in Unity's prefab blue.
        Entity PrefabRootOf(Entity entity) {
            for (Entity current = entity; current; current = current.GetComponent<HierarchyComponent>().Parent) {
                if (current.HasComponent<PrefabInstanceComponent>())
                    return current;
            }
            return {};
        }

        bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
            if (needle.empty())
                return true;
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            return it != haystack.end();
        }

        std::string PathOf(Entity entity) {
            std::string path = entity.GetComponent<NameComponent>().Name;
            for (Entity parent = entity.GetComponent<HierarchyComponent>().Parent; parent; parent = parent.GetComponent<HierarchyComponent>().Parent)
                path = parent.GetComponent<NameComponent>().Name + "/" + path;
            return path;
        }
    }

    void HierarchyPanel::BeginRename(EditorContext& ctx, Entity entity) {
        if (!IsLive(ctx, entity))
            return;
        m_RenamingId = ctx.Session.Ids.Get(entity);
        std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", entity.GetComponent<NameComponent>().Name.c_str());
        m_FocusRename = true;
        // Make the row visible (its parents expanded) so the field can actually be shown.
        for (Entity parent = entity.GetComponent<HierarchyComponent>().Parent; parent; parent = parent.GetComponent<HierarchyComponent>().Parent)
            m_Expanded.insert(ctx.Session.Ids.Get(parent));
        m_ScrollToId = m_RenamingId;
        m_SearchBuffer[0] = '\0';
    }

    void HierarchyPanel::SetExpandedRecursive(EditorContext& ctx, Entity entity, bool expanded) {
        const uint64_t id = ctx.Session.Ids.Get(entity);
        if (expanded)
            m_Expanded.insert(id);
        else
            m_Expanded.erase(id);
        for (Entity child : entity.GetComponent<HierarchyComponent>().Children)
            SetExpandedRecursive(ctx, child, expanded);
    }

    void HierarchyPanel::RevealSelection(EditorContext& ctx) {
        for (Entity entity : ctx.SelectedEntities) {
            if (!IsLive(ctx, entity))
                continue;
            for (Entity parent = entity.GetComponent<HierarchyComponent>().Parent; parent; parent = parent.GetComponent<HierarchyComponent>().Parent)
                m_Expanded.insert(ctx.Session.Ids.Get(parent));
        }
        m_SceneExpanded = true;
        if (ctx.Selected)
            m_ScrollToId = ctx.Session.Ids.Get(ctx.Selected);
    }

    void HierarchyPanel::HandleRowClick(EditorContext& ctx, Entity entity) {
        const ImGuiIO& io = ImGui::GetIO();
        const uint64_t id = ctx.Session.Ids.Get(entity);
        m_ClickedRowThisFrame = true;
        if (io.KeyShift) {
            // Range over the rows as displayed (Unity / Explorer behaviour).
            Entity anchor = ctx.Session.Ids.Find(ctx.SceneRef, m_RangeAnchorId);
            const std::vector<Entity>& rows = m_LastVisibleRows.empty() ? m_VisibleRows : m_LastVisibleRows;
            auto first = std::find(rows.begin(), rows.end(), anchor);
            auto last = std::find(rows.begin(), rows.end(), entity);
            if (anchor && first != rows.end() && last != rows.end()) {
                if (first > last)
                    std::swap(first, last);
                std::vector<Entity> range(first, last + 1);
                if (io.KeyCtrl) {
                    for (Entity existing : ctx.SelectedEntities)
                        if (std::find(range.begin(), range.end(), existing) == range.end())
                            range.push_back(existing);
                }
                EditorCommands::SetSelection(ctx, range, entity);
                return;
            }
        }
        if (io.KeyCtrl) {
            EditorCommands::ToggleSelected(ctx, entity);
            m_RangeAnchorId = id;
            return;
        }
        m_RangeAnchorId = id;
        if (EditorCommands::IsSelected(ctx, entity) && ctx.SelectedEntities.size() > 1) {
            // Keep the multi-selection for a possible drag; collapse to this row on release.
            m_PendingClickId = id;
            ctx.Selected = entity;
            return;
        }
        EditorCommands::SelectOnly(ctx, entity);
    }

    void HierarchyPanel::DrawRowContextMenu(EditorContext& ctx, Entity entity) {
        EditorCommands::DrawEntityEditMenuItems(ctx);
        ImGui::Separator();
        if (ImGui::MenuItem("Select Children")) {
            std::vector<Entity> children = entity.GetComponent<HierarchyComponent>().Children;
            if (!children.empty())
                EditorCommands::SetSelection(ctx, children, children.front());
        }
        if (ImGui::MenuItem("Expand All Children"))
            SetExpandedRecursive(ctx, entity, true);
        if (ImGui::MenuItem("Collapse All Children"))
            SetExpandedRecursive(ctx, entity, false);
        if (ImGui::MenuItem("Create Empty Parent", "Ctrl+Shift+G"))
            EditorCommands::CreateEmptyParent(ctx);
        ImGui::Separator();
        if (ImGui::BeginMenu("Prefab")) {
            Entity prefabRoot = PrefabRootOf(entity);
            const std::string prefabPath = prefabRoot ? AssetDatabase::ResolvePath(prefabRoot.GetComponent<PrefabInstanceComponent>().Prefab.Guid) : std::string();
            if (ImGui::MenuItem(prefabRoot ? "Save as New Prefab" : "Create Prefab"))
                EditorCommands::CreatePrefab(ctx, entity);
            if (ImGui::MenuItem("Open Asset in Context", nullptr, false, !prefabPath.empty()))
                ctx.RequestOpenPrefabPath = prefabPath;
            if (ImGui::MenuItem("Select Asset", nullptr, false, !prefabPath.empty())) {
                ctx.Session.PingAssetPath = prefabPath;
                ctx.Session.PingStartTime = ImGui::GetTime();
            }
            if (ImGui::MenuItem("Unpack", nullptr, false, prefabRoot == entity)) {
                if (PrefabSerializer::Unpack(entity))
                    MarkSceneDirty(ctx, "Unpack Prefab");
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        EditorCommands::DrawCreateObjectMenu(ctx, entity);
    }

    void HierarchyPanel::DrawEntityRow(EditorContext& ctx, Entity entity, int depth) {
        auto& hierarchy = entity.GetComponent<HierarchyComponent>();
        const uint64_t id = ctx.Session.Ids.Get(entity);
        const std::string& name = entity.GetComponent<NameComponent>().Name;
        m_VisibleRows.push_back(entity);

        // Leaf state is snapshotted before TreeNodeEx: a drop accepted on this very row can give
        // it its first child, and TreePop must match what TreeNodeEx actually pushed.
        const bool wasLeaf = hierarchy.Children.empty();
        const bool expanded = !wasLeaf && m_Expanded.count(id) != 0;
        const bool selected = EditorCommands::IsSelected(ctx, entity);
        const bool renaming = m_RenamingId == id;

        ImGui::PushID(static_cast<int>(id));
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;
        if (!renaming)
            flags |= ImGuiTreeNodeFlags_SpanFullWidth;
        if (selected)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (wasLeaf)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // Unity colors: prefab instances blue (red when the asset is missing), inactive grey.
        const bool active = ctx.SceneRef.IsEffectivelyActive(entity);
        Entity prefabRoot = PrefabRootOf(entity);
        ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        if (prefabRoot) {
            const bool missing = AssetDatabase::ResolvePath(prefabRoot.GetComponent<PrefabInstanceComponent>().Prefab.Guid).empty();
            textColor = missing ? EditorColors::PrefabMissing() : EditorColors::PrefabText();
        }
        if (!active) {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(textColor);
            c.w *= 0.5f;
            textColor = ImGui::ColorConvertFloat4ToU32(c);
        }

        // Ping highlight (an object selected from a reference field / the Scene view).
        const double pingAge = ImGui::GetTime() - ctx.Session.PingStartTime;
        if (ctx.Session.PingEntityId == id && pingAge < 1.5) {
            const ImVec2 min(ImGui::GetWindowPos().x, ImGui::GetCursorScreenPos().y);
            const ImVec2 max(min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetFrameHeight());
            const float alpha = static_cast<float>(1.0 - pingAge / 1.5);
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, (EditorColors::Ping() & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 110.0f) << 24));
        }

        ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()));
        const std::string label = renaming ? std::string(IconForEntity(entity)) : std::string(IconForEntity(entity)) + "  " + name;
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(id)), flags, "%s", label.c_str());
        if (selected)
            ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();

        if (m_ScrollToId == id) {
            ImGui::SetScrollHereY(0.4f);
            m_ScrollToId = 0;
        }

        // Fold toggles (Alt = the whole branch, Unity).
        if (ImGui::IsItemToggledOpen()) {
            const bool nowOpen = !expanded;
            if (ImGui::GetIO().KeyAlt)
                SetExpandedRecursive(ctx, entity, nowOpen);
            else if (nowOpen)
                m_Expanded.insert(id);
            else
                m_Expanded.erase(id);
        } else if (!renaming) {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                HandleRowClick(ctx, entity);
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_PendingClickId == id &&
                ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < ImGui::GetIO().MouseDragThreshold * ImGui::GetIO().MouseDragThreshold) {
                EditorCommands::SelectOnly(ctx, entity);
                m_PendingClickId = 0;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                m_ClickedRowThisFrame = true;
                if (!selected)
                    EditorCommands::SelectOnly(ctx, entity);
            }
            // Double-click frames the object in the Scene view (Unity).
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
                ctx.Session.RequestFrameSelected = true;
        }

        if (!renaming && ImGui::BeginDragDropSource()) {
            if (!EditorCommands::IsSelected(ctx, entity))
                EditorCommands::SelectOnly(ctx, entity);
            m_PendingClickId = 0;
            entt::entity handle = entity.Handle();
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &handle, sizeof(handle));
            const size_t count = EditorCommands::TopLevelSelection(ctx).size();
            if (count > 1)
                ImGui::Text("%s  %s (+%zu)", IconForEntity(entity), name.c_str(), count - 1);
            else
                ImGui::Text("%s  %s", IconForEntity(entity), name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            // Top quarter = before this row, bottom quarter = after it (first child when the row
            // is expanded), middle = make it a child -- Unity's three drop zones per row.
            Entity dragged = DraggedHierarchyEntity(ctx);
            const float edge = (rowMax.y - rowMin.y) * 0.25f;
            const float mouseY = ImGui::GetIO().MousePos.y;
            enum class Zone { Before, Child, After } zone = mouseY < rowMin.y + edge ? Zone::Before : (mouseY > rowMax.y - edge ? Zone::After : Zone::Child);
            Entity parent = zone == Zone::Child ? entity : hierarchy.Parent;
            bool asFirstChild = zone == Zone::After && expanded;
            if (asFirstChild)
                parent = entity;
            if (dragged) {
                const std::vector<Entity> moved = MovedEntities(ctx, dragged);
                const bool targetIsMoved = std::find(moved.begin(), moved.end(), entity) != moved.end();
                const bool valid = !targetIsMoved && CanMoveUnder(moved, parent);
                if (valid) {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImU32 highlight = IM_COL32(80, 160, 255, 255);
                    const float indent = ImGui::GetTreeNodeToLabelSpacing();
                    if (zone == Zone::Child)
                        drawList->AddRect(rowMin, rowMax, highlight, 2.0f, 0, 2.0f);
                    else {
                        const float y = zone == Zone::Before ? rowMin.y : rowMax.y;
                        const float x = rowMin.x + (asFirstChild ? indent : 0.0f) + indent * 0.5f;
                        drawList->AddLine(ImVec2(x, y), ImVec2(rowMax.x, y), highlight, 2.0f);
                        drawList->AddCircle(ImVec2(x, y), 3.0f, highlight, 8, 1.5f);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    if (payload->Delivery && valid) {
                        if (zone == Zone::Child)
                            MoveEntities(ctx, moved, entity, {}, false, SIZE_MAX);
                        else if (asFirstChild)
                            MoveEntities(ctx, moved, entity, {}, false, 0);
                        else
                            MoveEntities(ctx, moved, parent, entity, zone == Zone::After, SIZE_MAX);
                        m_Expanded.insert(ctx.Session.Ids.Get(parent));
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                if (payload->Delivery && InstantiateDroppedPrefab(ctx, payload, entity))
                    m_Expanded.insert(id);
            }
            ImGui::EndDragDropTarget();
        }

        if (!renaming && ImGui::BeginPopupContextItem("EntityContext")) {
            if (ImGui::IsWindowAppearing() && !EditorCommands::IsSelected(ctx, entity))
                EditorCommands::SelectOnly(ctx, entity);
            DrawRowContextMenu(ctx, entity);
            ImGui::EndPopup();
        }

        // Inline rename field drawn over the row label.
        if (renaming) {
            ImGui::SameLine();
            if (m_FocusRename) {
                ImGui::SetKeyboardFocusHere();
                m_FocusRename = false;
            }
            ImGui::SetNextItemWidth(-1.0f);
            const bool submitted = ImGui::InputText("##Rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool cancelled = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (cancelled) {
                m_RenamingId = 0;
            } else if (submitted || ImGui::IsItemDeactivated()) {
                if (m_RenameBuffer[0] != '\0' && name != m_RenameBuffer) {
                    entity.GetComponent<NameComponent>().Name = m_RenameBuffer;
                    MarkSceneDirty(ctx, "Rename");
                }
                m_RenamingId = 0;
            }
        } else {
            // Active toggle at the row's right edge (Unity shows it in the Inspector header; the
            // Hierarchy eye toggle is the common shortcut for it).
            const bool selfActive = entity.GetComponent<ActiveComponent>().Active;
            const float toggleWidth = ImGui::GetFrameHeight();
            ImGui::SameLine(ImGui::GetContentRegionMax().x - toggleWidth);
            const bool hovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);
            if (hovered || !selfActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
                ImGui::PushStyleColor(ImGuiCol_Text, selfActive ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                if (ImGui::Button(selfActive ? EditorIcons::Eye : EditorIcons::EyeSlash, ImVec2(toggleWidth, 0.0f))) {
                    // Applies to the whole selection when this row is part of it.
                    if (EditorCommands::IsSelected(ctx, entity)) {
                        for (Entity target : ctx.SelectedEntities)
                            EditorCommands::SetActive(ctx, target, !selfActive);
                    } else {
                        EditorCommands::SetActive(ctx, entity, !selfActive);
                    }
                    m_ClickedRowThisFrame = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip(selfActive ? "Deactivate (Alt+Shift+A)" : "Activate (Alt+Shift+A)");
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
            } else {
                ImGui::Dummy(ImVec2(toggleWidth, ImGui::GetFrameHeight()));
            }
        }

        if (!wasLeaf && open) {
            // Copy: a drop accepted while drawing a child mutates the live Children vector.
            std::vector<Entity> children = hierarchy.Children;
            for (Entity child : children) {
                if (IsLive(ctx, child))
                    DrawEntityRow(ctx, child, depth + 1);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void HierarchyPanel::DrawSearchResults(EditorContext& ctx) {
        for (Entity entity : ctx.SceneRef.GetHierarchyTraversalOrder()) {
            const std::string& name = entity.GetComponent<NameComponent>().Name;
            if (!ContainsCaseInsensitive(name, m_SearchBuffer))
                continue;
            m_VisibleRows.push_back(entity);
            const uint64_t id = ctx.Session.Ids.Get(entity);
            ImGui::PushID(static_cast<int>(id));
            const bool selected = EditorCommands::IsSelected(ctx, entity);
            const std::string label = std::string(IconForEntity(entity)) + "  " + name;
            if (ImGui::Selectable(label.c_str(), selected))
                HandleRowClick(ctx, entity);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", PathOf(entity).c_str());
            if (ImGui::BeginPopupContextItem("SearchContext")) {
                if (ImGui::IsWindowAppearing() && !selected)
                    EditorCommands::SelectOnly(ctx, entity);
                DrawRowContextMenu(ctx, entity);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }

    void HierarchyPanel::HandleKeyboardNavigation(EditorContext& ctx) {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput || m_LastVisibleRows.empty())
            return;
        const ImGuiIO& io = ImGui::GetIO();
        auto current = std::find(m_LastVisibleRows.begin(), m_LastVisibleRows.end(), ctx.Selected);
        auto selectRow = [&](Entity entity) {
            if (io.KeyShift && ctx.Selected) {
                std::vector<Entity> extended = ctx.SelectedEntities;
                extended.push_back(entity);
                EditorCommands::SetSelection(ctx, extended, entity);
            } else {
                EditorCommands::SelectOnly(ctx, entity);
                m_RangeAnchorId = ctx.Session.Ids.Get(entity);
            }
            m_ScrollToId = ctx.Session.Ids.Get(entity);
        };
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            if (current == m_LastVisibleRows.end())
                selectRow(m_LastVisibleRows.front());
            else if (current + 1 != m_LastVisibleRows.end())
                selectRow(*(current + 1));
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            if (current == m_LastVisibleRows.end())
                selectRow(m_LastVisibleRows.back());
            else if (current != m_LastVisibleRows.begin())
                selectRow(*(current - 1));
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && ctx.Selected) {
            if (io.KeyAlt)
                SetExpandedRecursive(ctx, ctx.Selected, true);
            else
                m_Expanded.insert(ctx.Session.Ids.Get(ctx.Selected));
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && ctx.Selected) {
            const uint64_t id = ctx.Session.Ids.Get(ctx.Selected);
            Entity parent = ctx.Selected.GetComponent<HierarchyComponent>().Parent;
            if (io.KeyAlt)
                SetExpandedRecursive(ctx, ctx.Selected, false);
            else if (m_Expanded.count(id) && !ctx.Selected.GetComponent<HierarchyComponent>().Children.empty())
                m_Expanded.erase(id);
            else if (parent)
                selectRow(parent);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && ctx.Selected) {
            BeginRename(ctx, ctx.Selected);
        }
    }

    void HierarchyPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.Session.ShowHierarchy)
            return;
        if (!ImGui::Begin("Hierarchy", &ctx.Session.ShowHierarchy)) {
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ctx.Session.FocusedPanel = EditorPanel::Hierarchy;

        if (ctx.Session.RequestRevealSelection) {
            ctx.Session.RequestRevealSelection = false;
            RevealSelection(ctx);
        }
        // A reference field pinged an entity: make its row visible.
        if (ctx.Session.PingEntityId && ctx.Session.PingStartTime != m_HandledPingTime) {
            m_HandledPingTime = ctx.Session.PingStartTime;
            if (Entity pinged = ctx.Session.Ids.Find(ctx.SceneRef, ctx.Session.PingEntityId)) {
                for (Entity parent = pinged.GetComponent<HierarchyComponent>().Parent; parent; parent = parent.GetComponent<HierarchyComponent>().Parent)
                    m_Expanded.insert(ctx.Session.Ids.Get(parent));
                m_SceneExpanded = true;
                m_ScrollToId = ctx.Session.PingEntityId;
                m_SearchBuffer[0] = 0;
            }
        }
        if (ctx.Session.RequestRename) {
            ctx.Session.RequestRename = false;
            BeginRename(ctx, ctx.Selected);
        }
        if (m_RenamingId && !ctx.Session.Ids.Find(ctx.SceneRef, m_RenamingId))
            m_RenamingId = 0;

        // ---- Toolbar: "+" create menu and search (Unity's Hierarchy header).
        if (ImGui::Button((std::string(EditorIcons::Add) + " " + EditorIcons::CaretDown).c_str()))
            ImGui::OpenPopup("HierarchyCreate");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Create a GameObject");
        if (ImGui::BeginPopup("HierarchyCreate")) {
            EditorCommands::DrawCreateObjectMenu(ctx);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchySearch", (std::string(EditorIcons::Search) + " Search").c_str(), m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::Separator();

        m_LastVisibleRows = std::move(m_VisibleRows);
        m_VisibleRows.clear();
        m_ClickedRowThisFrame = false;

        ImGui::BeginChild("HierarchyTree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
        if (m_SearchBuffer[0] != '\0') {
            DrawSearchResults(ctx);
        } else {
            // The Scene is the synthetic root (Unity's scene header row): not an Entity.
            std::string sceneName = ctx.IsEditingPrefab ? std::filesystem::path(ctx.EditingPrefabPath).stem().string()
                                                        : std::filesystem::path(ctx.ScenePath).stem().string();
            if (sceneName.empty())
                sceneName = "Untitled";
            const std::string sceneLabel = std::string(ctx.IsEditingPrefab ? EditorIcons::Cube : EditorIcons::Map) + "  " + sceneName + (ctx.SceneDirty ? "*" : "");

            ImGui::PushID("SceneHierarchyRoot");
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
            ImGui::SetNextItemOpen(m_SceneExpanded, ImGuiCond_Always);
            ImGui::PushFont(EditorBoldFont());
            const bool sceneOpen = ImGui::TreeNodeEx(sceneLabel.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding);
            ImGui::PopFont();
            ImGui::PopStyleColor();
            if (ImGui::IsItemToggledOpen())
                m_SceneExpanded = !m_SceneExpanded;

            if (ImGui::BeginDragDropTarget()) {
                if (Entity dragged = DraggedHierarchyEntity(ctx)) {
                    const std::vector<Entity> moved = MovedEntities(ctx, dragged);
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
                        if (payload->Delivery)
                            MoveEntities(ctx, moved, Entity{}, {}, false, SIZE_MAX);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
                    if (payload->Delivery)
                        InstantiateDroppedPrefab(ctx, payload, Entity{});
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("SceneRootContext")) {
                if (ImGui::MenuItem(ctx.IsEditingPrefab ? "Save Prefab" : "Save Scene", "Ctrl+S"))
                    SaveScene(ctx);
                if (!ctx.IsEditingPrefab && ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                    ctx.RequestSaveSceneAs = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, EditorCommands::HasClipboard(ctx))) {
                    EditorCommands::ClearSelection(ctx);
                    EditorCommands::Paste(ctx, false);
                }
                ImGui::Separator();
                EditorCommands::DrawCreateObjectMenu(ctx);
                ImGui::EndPopup();
            }

            if (sceneOpen) {
                std::vector<Entity> roots = ctx.SceneRef.GetRootEntities();
                for (Entity entity : roots) {
                    if (IsLive(ctx, entity))
                        DrawEntityRow(ctx, entity, 0);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar(2);

        // Blank space below the rows: drop target (move to scene root / instantiate prefab),
        // click to deselect, right-click for the create menu.
        ImVec2 dropZoneSize = ImGui::GetContentRegionAvail();
        dropZoneSize.x = std::max(dropZoneSize.x, 1.0f);
        dropZoneSize.y = std::max(dropZoneSize.y, ImGui::GetFrameHeight());
        ImGui::InvisibleButton("##RootDropZone", dropZoneSize);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
            EditorCommands::ClearSelection(ctx);
        if (ImGui::BeginDragDropTarget()) {
            if (Entity dragged = DraggedHierarchyEntity(ctx)) {
                const std::vector<Entity> moved = MovedEntities(ctx, dragged);
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
                    if (payload->Delivery)
                        MoveEntities(ctx, moved, Entity{}, {}, false, SIZE_MAX);
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID"))
                if (payload->Delivery)
                    InstantiateDroppedPrefab(ctx, payload, Entity{});
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("HierarchyBlankContext")) {
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, EditorCommands::HasClipboard(ctx))) {
                EditorCommands::ClearSelection(ctx);
                EditorCommands::Paste(ctx, false);
            }
            ImGui::Separator();
            EditorCommands::DrawCreateObjectMenu(ctx);
            ImGui::EndPopup();
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            m_PendingClickId = 0;

        HandleKeyboardNavigation(ctx);
        ImGui::EndChild();
        ImGui::End();
    }

}
