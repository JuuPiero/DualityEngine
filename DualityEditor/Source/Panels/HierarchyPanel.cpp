#include "DualityEditor/Panels/HierarchyPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"

namespace Duality {

    namespace {
        // entt::entity's underlying type is small enough to round-trip through a
        // plain int -- used both for ImGui::PushID (needs an int/ptr, not an enum
        // class) and as the drag-drop payload value.
        int EntityId(Entity entity) { return static_cast<int>(static_cast<uint32_t>(entity.Handle())); }

        // A root entity's own screen, if it has one -- ScreenGroupComponent takes
        // priority (explicit organizational tag), falling back to CameraComponent
        // (a camera already carries a real Screen, no separate tag needed). Only
        // consulted for ROOT entities: the split below is a top-level display
        // grouping, not a per-node reclassification -- once inside a Top/Bottom
        // section, a subtree renders exactly as its actual parent/child structure
        // says, same as before this feature existed.
        bool TryResolveRootScreen(Entity root, Screen& outScreen) {
            if (root.HasComponent<ScreenGroupComponent>()) {
                outScreen = root.GetComponent<ScreenGroupComponent>().Screen;
                return true;
            }
            if (root.HasComponent<CameraComponent>()) {
                outScreen = root.GetComponent<CameraComponent>().Screen;
                return true;
            }
            return false;
        }

        void AcceptReparentDrop(EditorContext& ctx, Entity newParent, Entity insertAfter = {}) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                entt::entity draggedHandle = *static_cast<const entt::entity*>(payload->Data);
                Entity dragged(draggedHandle, &ctx.SceneRef);
                ctx.SceneRef.SetParent(dragged, newParent, insertAfter);
            }
        }

        // Dropping directly onto a section header (or the catch-all space below all
        // three) moves the dragged entity into that section: unparented to root first
        // (the split only classifies roots, see TryResolveRootScreen) since a nested
        // entity's section follows its root ancestor's tag, not its own -- then its
        // ScreenGroupComponent is set/added (targetScreen non-null) or removed
        // (nullptr, "Ungrouped"). A camera entity dropped into "Ungrouped" stays
        // classified by its own CameraComponent::Screen regardless -- that's its real
        // target screen, not just an organizational tag, so it isn't cleared here.
        void AcceptSectionDrop(EditorContext& ctx, const Screen* targetScreen) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                entt::entity draggedHandle = *static_cast<const entt::entity*>(payload->Data);
                Entity dragged(draggedHandle, &ctx.SceneRef);
                ctx.SceneRef.SetParent(dragged, Entity{});
                if (targetScreen) {
                    if (dragged.HasComponent<ScreenGroupComponent>())
                        dragged.GetComponent<ScreenGroupComponent>().Screen = *targetScreen;
                    else
                        dragged.AddComponent<ScreenGroupComponent>().Screen = *targetScreen;
                } else if (dragged.HasComponent<ScreenGroupComponent>()) {
                    dragged.RemoveComponent<ScreenGroupComponent>();
                }
            }
        }

        // A thin invisible drop target between rows: dropping here reorders the
        // dragged entity as the next sibling after `insertAfter`, under
        // `insertAfter`'s own parent -- this is what "drag up/down to reorder"
        // means once a real tree exists (dropping ON a row instead makes the
        // dragged entity that row's *child*, handled directly in DrawEntityNode).
        // Writes `entity` (+ its full descendant subtree) to a new
        // "Assets/Prefabs/<EntityName>.prefab" file -- same 3-step
        // Save -> AssetMeta::EnsureMetaFile -> AssetDatabase::Register sequence already
        // established for creating any other custom asset type (see Application.cpp's
        // TestOrange.mat setup). ctx.ScenePath is "<AssetsDir>/Scene.scene"
        // (see Application.cpp), so its parent directory recovers AssetsDir without
        // EditorContext needing its own dedicated field for it.
        void CreatePrefabFromSelection(EditorContext& ctx, Entity entity) {
            std::filesystem::path assetsDir = std::filesystem::path(ctx.ScenePath).parent_path();
            std::filesystem::path prefabsDir = assetsDir / "Prefabs";
            std::filesystem::create_directories(prefabsDir);

            std::string entityName = entity.GetComponent<NameComponent>().Name;
            std::filesystem::path prefabPath = prefabsDir / (entityName + ".prefab");

            if (!PrefabSerializer::Save(entity, prefabPath.string()))
                return;
            std::string guid = AssetMeta::EnsureMetaFile(prefabPath);
            AssetDatabase::Register(guid, prefabPath.string());
        }

        // Case-insensitive substring match, same small local-helper shape as the
        // identical need in ConsolePanel.cpp -- not worth a shared utility for one line.
        bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
            if (needle.empty())
                return true;
            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            return it != haystack.end();
        }

        // A search box over a tree with drag-drop reparenting either has to hide/show
        // whole subtrees (auto-expanding ancestors of any match) or fall back to a
        // simpler flat list while searching -- this takes the flat-list route: while the
        // search box has text, every matching entity in the WHOLE scene is listed (no
        // tree structure, no drag-drop), still clickable to select. Empty search box
        // shows the normal full tree UI, completely unchanged.
        void DrawFilteredFlatList(EditorContext& ctx, const std::string& filter) {
            for (auto handle : ctx.SceneRef.Registry().view<NameComponent>()) {
                Entity entity(handle, &ctx.SceneRef);
                const std::string& name = entity.GetComponent<NameComponent>().Name;
                if (!ContainsCaseInsensitive(name, filter))
                    continue;

                ImGui::PushID(EntityId(entity));
                bool selected = (entity == ctx.Selected);
                if (ImGui::Selectable(name.c_str(), selected))
                    ctx.Selected = entity;
                ImGui::PopID();
            }
        }

        void DrawSiblingGap(EditorContext& ctx, Entity parent, Entity insertAfter) {
            ImGui::InvisibleButton("##Gap", ImVec2(-1.0f, 4.0f));
            if (ImGui::BeginDragDropTarget()) {
                AcceptReparentDrop(ctx, parent, insertAfter);
                ImGui::EndDragDropTarget();
            }
        }

        void DrawEntityNode(Entity entity, EditorContext& ctx) {
            auto& hierarchy = entity.GetComponent<HierarchyComponent>();
            const std::string& name = entity.GetComponent<NameComponent>().Name;

            // Snapshotted BEFORE TreeNodeEx runs, and used for the TreePop decision
            // below instead of re-reading hierarchy.Children live -- the drag-drop
            // target a few lines down can append a new child to THIS SAME node
            // (dropping something onto a currently-childless node is the most
            // common way a user creates their first parent/child relationship).
            // TreeNodeEx already committed to NoTreePushOnOpen (no push at all)
            // based on the leaf state at call time; re-checking Children.empty()
            // afterward and finding it non-empty then wrongly called TreePop()
            // against a push that never happened, corrupting ImGui's ID/tree stack
            // and crashing -- this was the actual "crash when dragging an entity
            // onto another" bug.
            bool wasLeaf = hierarchy.Children.empty();

            ImGui::PushID(EntityId(entity));

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (entity == ctx.Selected)
                flags |= ImGuiTreeNodeFlags_Selected;
            if (wasLeaf)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            // Cheap visual grouping, not a structural Hierarchy split -- entities
            // aren't exclusively owned by a screen in this engine, so a hard
            // partition of the tree would be dishonest. Same Top/Bottom colors as
            // the Scene view's own camera markers (ScenePanel.cpp).
            if (entity.HasComponent<ScreenGroupComponent>()) {
                Screen screen = entity.GetComponent<ScreenGroupComponent>().Screen;
                ImVec4 color = (screen == Screen::Top) ? ImVec4(0.3f, 0.9f, 0.9f, 1.0f) : ImVec4(0.95f, 0.6f, 0.2f, 1.0f);
                ImGui::ColorButton("##ScreenGroupMarker", color,
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoDragDrop, ImVec2(8, 8));
                ImGui::SameLine();
            }

            bool open = ImGui::TreeNodeEx(name.c_str(), flags);
            if (ImGui::IsItemClicked())
                ctx.Selected = entity;

            if (ImGui::BeginDragDropSource()) {
                entt::entity handle = entity.Handle();
                ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &handle, sizeof(handle));
                ImGui::Text("%s", name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                AcceptReparentDrop(ctx, entity); // dropped ON this node -> becomes its child
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Create Child Entity")) {
                    Entity child = ctx.SceneRef.CreateEntity("Entity");
                    ctx.SceneRef.SetParent(child, entity);
                    ctx.Selected = child;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Create Prefab from Selection"))
                    CreatePrefabFromSelection(ctx, entity);
                ImGui::EndPopup();
            }

            // Matches TreeNodeEx exactly: it pushed a tree node iff (!wasLeaf &&
            // open), so that's the only condition under which TreePop is valid.
            if (!wasLeaf && open) {
                // Copy, not a reference -- a drop accepted while drawing one child
                // (e.g. reparenting a sibling onto it) mutates the *live*
                // Children vector, which would invalidate this range-for if it
                // were iterating that vector directly.
                std::vector<Entity> children = hierarchy.Children;
                for (Entity child : children)
                    DrawEntityNode(child, ctx);
                ImGui::TreePop();
            }

            // Insert-after-this-node reorder gap, in this node's own parent's
            // sibling list (root list if this node itself is a root).
            DrawSiblingGap(ctx, hierarchy.Parent, entity);

            ImGui::PopID();
        }
    }

    void HierarchyPanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Hierarchy");

        if (ImGui::Button("Create Entity", ImVec2(-1, 0)))
            ctx.Selected = ctx.SceneRef.CreateEntity("Entity");

        ImGui::InputTextWithHint("##HierarchySearch", "Search...", m_SearchBuffer, sizeof(m_SearchBuffer));

        ImGui::Separator();

        // While searching, show a flat filtered list instead of the normal tree --
        // see DrawFilteredFlatList's own comment for why (a tree with drag-drop
        // reparenting doesn't have an obvious cheap way to hide/show whole subtrees).
        if (m_SearchBuffer[0] != '\0') {
            DrawFilteredFlatList(ctx, m_SearchBuffer);
            ImGui::End();
            return;
        }

        // Copy, same reasoning as DrawEntityNode's own Children copy -- a drop
        // during this pass can reparent a root entity elsewhere mid-iteration.
        // Split into 3 stacked sections by each ROOT's own resolved screen (see
        // TryResolveRootScreen) -- an entity nested under a Top/Bottom-tagged root
        // shows up only in that section, matching what was asked; anything that
        // hasn't been tagged (or isn't a camera) goes to "Ungrouped" rather than
        // being silently hidden from the Hierarchy entirely. Root order (and thus
        // drag-reorder/reparent) is still one flat list underneath (Scene::
        // m_RootEntities) -- this split is a display filter over it, not a second
        // data structure, so dragging a root across a section boundary via the
        // sibling gap (as opposed to dropping it directly onto a node to reparent)
        // reorders it in that flat list without necessarily landing in the section
        // the gap visually belonged to; dropping ON a node always works correctly.
        std::vector<Entity> roots = ctx.SceneRef.GetRootEntities();
        std::vector<Entity> topRoots, bottomRoots, ungroupedRoots;
        for (Entity entity : roots) {
            Screen screen;
            if (TryResolveRootScreen(entity, screen))
                (screen == Screen::Top ? topRoots : bottomRoots).push_back(entity);
            else
                ungroupedRoots.push_back(entity);
        }

        // Each header is also a drop target for moving an entity INTO that section
        // (AcceptSectionDrop) -- separate from dropping directly onto a row inside
        // a section, which still means "become that row's child" (DrawEntityNode).
        bool topOpen = ImGui::CollapsingHeader("Top Screen", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginDragDropTarget()) {
            Screen top = Screen::Top;
            AcceptSectionDrop(ctx, &top);
            ImGui::EndDragDropTarget();
        }
        if (topOpen)
            for (Entity entity : topRoots)
                DrawEntityNode(entity, ctx);

        bool bottomOpen = ImGui::CollapsingHeader("Bottom Screen", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginDragDropTarget()) {
            Screen bottom = Screen::Bottom;
            AcceptSectionDrop(ctx, &bottom);
            ImGui::EndDragDropTarget();
        }
        if (bottomOpen)
            for (Entity entity : bottomRoots)
                DrawEntityNode(entity, ctx);

        bool ungroupedOpen = ImGui::CollapsingHeader("Ungrouped", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginDragDropTarget()) {
            AcceptSectionDrop(ctx, nullptr);
            ImGui::EndDragDropTarget();
        }
        if (ungroupedOpen)
            for (Entity entity : ungroupedRoots)
                DrawEntityNode(entity, ctx);

        // Drop target filling the remaining panel space below the tree -- same as
        // dropping directly onto the "Ungrouped" header above. InvisibleButton
        // asserts on a zero-size axis, which GetContentRegionAvail() can return
        // when the tree already fills the panel -- clamp to a 1px minimum.
        ImVec2 dropZoneSize = ImGui::GetContentRegionAvail();
        dropZoneSize.x = std::max(dropZoneSize.x, 1.0f);
        dropZoneSize.y = std::max(dropZoneSize.y, 1.0f);
        ImGui::InvisibleButton("##RootDropZone", dropZoneSize);
        if (ImGui::BeginDragDropTarget()) {
            AcceptSectionDrop(ctx, nullptr);
            // A Prefab asset dropped here (not an existing HIERARCHY_ENTITY drag)
            // instantiates a new copy at the scene root -- PrefabSerializer::Instantiate
            // itself just logs an error and returns an empty Entity if the dropped
            // asset isn't actually a valid prefab file, so no extension check needed
            // here first.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                std::string guid = static_cast<const char*>(payload->Data);
                std::string path = AssetDatabase::ResolvePath(guid);
                if (!path.empty())
                    ctx.Selected = PrefabSerializer::Instantiate(ctx.SceneRef, path);
            }
            ImGui::EndDragDropTarget();
        }

        // Right-click anywhere in the panel (including empty space below the
        // list) for a Unity-style "Create Empty" context menu. NoOpenOverItems
        // lets each node's own BeginPopupContextItem (Create Child Entity) take
        // precedence when right-clicking directly on a row.
        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Entity"))
                ctx.Selected = ctx.SceneRef.CreateEntity("Entity");
            ImGui::EndPopup();
        }

        ImGui::End();
    }

}
