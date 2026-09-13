#include "DualityEditor/Panels/HierarchyPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Scene/PrefabSerializer.h"

namespace Duality {

    namespace {
        // entt::entity's underlying type is small enough to round-trip through a
        // plain int -- used both for ImGui::PushID (needs an int/ptr, not an enum
        // class) and as the drag-drop payload value.
        int EntityId(Entity entity) { return static_cast<int>(static_cast<uint32_t>(entity.Handle())); }

        // Used only by the one-shot migration action below. New scenes use one
        // Top/Bottom root and all of their children inherit the root's layer.
        bool TryResolveRootLayer(Entity root, Layer& outLayer) {
            if (root.HasComponent<LayerComponent>()) {
                outLayer = root.GetComponent<LayerComponent>().Value;
                return outLayer != Layer::Default;
            }
            if (root.HasComponent<CameraComponent>()) {
                outLayer = ScreenToLayer(root.GetComponent<CameraComponent>().Screen);
                return true;
            }
            return false;
        }

        void AcceptReparentDrop(EditorContext& ctx, Entity newParent, Entity insertAfter = {}) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                entt::entity draggedHandle = *static_cast<const entt::entity*>(payload->Data);
                Entity dragged(draggedHandle, &ctx.SceneRef);
                ctx.SceneRef.SetParent(dragged, newParent, insertAfter);
                MarkSceneDirty(ctx);
            }
        }

        void EnsureScreenRoots(EditorContext& ctx) {
            const bool hadTop = static_cast<bool>(ctx.SceneRef.GetScreenRoot(Screen::Top));
            const bool hadBottom = static_cast<bool>(ctx.SceneRef.GetScreenRoot(Screen::Bottom));
            ctx.SceneRef.EnsureScreenRoot(Screen::Top);
            ctx.SceneRef.EnsureScreenRoot(Screen::Bottom);
            if (!hadTop || !hadBottom)
                MarkSceneDirty(ctx);
        }

        // Converts the previous section-based layout into two honest hierarchy
        // roots. Redundant LayerComponents are removed from the moved roots so
        // their descendants inherit from Top/Bottom instead.
        void OrganizeExistingScreenRoots(EditorContext& ctx) {
            const bool hadTop = static_cast<bool>(ctx.SceneRef.GetScreenRoot(Screen::Top));
            const bool hadBottom = static_cast<bool>(ctx.SceneRef.GetScreenRoot(Screen::Bottom));
            std::vector<Entity> roots = ctx.SceneRef.GetRootEntities();
            Entity top = ctx.SceneRef.EnsureScreenRoot(Screen::Top);
            Entity bottom = ctx.SceneRef.EnsureScreenRoot(Screen::Bottom);
            bool changed = false;
            for (Entity root : roots) {
                if (root == top || root == bottom)
                    continue;
                Layer layer;
                if (!TryResolveRootLayer(root, layer) || layer == Layer::Default)
                    continue;
                Entity target = layer == Layer::TOP ? top : bottom;
                ctx.SceneRef.SetParent(root, target);
                if (root.HasComponent<LayerComponent>() && root.GetComponent<LayerComponent>().Value == layer)
                    root.RemoveComponent<LayerComponent>();
                changed = true;
            }
            if (changed || !hadTop || !hadBottom)
                MarkSceneDirty(ctx);
        }

        // A minimal, useful 3DS scene needs a real camera for each physical
        // screen. This bootstrap deliberately stays 2D/orthographic: it is a
        // safe starting point for sprites, tilemaps and touch raycasts; a game
        // that needs 3D can switch either Camera component in the Inspector.
        void Bootstrap3DSScene(EditorContext& ctx) {
            const bool hadTopRoot = static_cast<bool>(ctx.SceneRef.GetScreenRoot(Screen::Top));
            const bool hadBottomRoot = static_cast<bool>(ctx.SceneRef.GetScreenRoot(Screen::Bottom));
            Entity topRoot = ctx.SceneRef.EnsureScreenRoot(Screen::Top);
            Entity bottomRoot = ctx.SceneRef.EnsureScreenRoot(Screen::Bottom);
            bool changed = !hadTopRoot || !hadBottomRoot;

            auto createCameraIfMissing = [&](Screen screen, Entity parent) {
                if (ctx.SceneRef.GetPrimaryCamera(screen))
                    return;
                Entity camera = ctx.SceneRef.CreateEntity(screen == Screen::Top ? "Top Camera" : "Bottom Camera");
                camera.AddComponent<CameraComponent>().Screen = screen;
                camera.AddComponent<PhysicsRaycaster2DComponent>();
                ctx.SceneRef.SetParent(camera, parent);
                changed = true;
            };
            createCameraIfMissing(Screen::Top, topRoot);
            createCameraIfMissing(Screen::Bottom, bottomRoot);

            if (changed) {
                ctx.Selected = topRoot;
                MarkSceneDirty(ctx);
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

        Entity CreateCanvas(EditorContext& ctx, Screen screen, Entity parent = {}) {
            // Canvas belongs to a physical screen. With no explicit parent, put
            // it beneath that screen's organizational root so UI follows the
            // same one-tree Top/Bottom convention as gameplay entities.
            if (!parent)
                parent = ctx.SceneRef.EnsureScreenRoot(screen);
            Entity canvas = ctx.SceneRef.CreateEntity(screen == Screen::Top ? "Top Canvas" : "Bottom Canvas");
            canvas.AddComponent<CanvasComponent>().Screen = screen;
            ctx.SceneRef.SetParent(canvas, parent);
            ctx.Selected = canvas;
            MarkSceneDirty(ctx);
            return canvas;
        }

        Entity CreateEmpty(EditorContext& ctx, Entity parent = {}) {
            Entity entity = ctx.SceneRef.CreateEntity("Entity");
            if (parent)
                ctx.SceneRef.SetParent(entity, parent);
            ctx.Selected = entity;
            MarkSceneDirty(ctx);
            return entity;
        }

        Entity CreateSprite(EditorContext& ctx, Entity parent = {}) {
            Entity sprite = ctx.SceneRef.CreateEntity("Sprite");
            sprite.AddComponent<SpriteRendererComponent>();
            if (parent)
                ctx.SceneRef.SetParent(sprite, parent);
            ctx.Selected = sprite;
            MarkSceneDirty(ctx);
            return sprite;
        }

        Entity CreatePrimitive(EditorContext& ctx, const char* name, MeshPrimitive primitive, Entity parent = {}) {
            Entity mesh = ctx.SceneRef.CreateEntity(name);
            mesh.AddComponent<MeshRendererComponent>().Primitive = primitive;
            if (parent)
                ctx.SceneRef.SetParent(mesh, parent);
            ctx.Selected = mesh;
            MarkSceneDirty(ctx);
            return mesh;
        }

        Entity FindCanvasAncestor(Entity entity) {
            while (entity) {
                if (entity.HasComponent<CanvasComponent>())
                    return entity;
                entity = entity.GetComponent<HierarchyComponent>().Parent;
            }
            return Entity{};
        }

        // Creates Unity's familiar Button bundle in one action: RectTransform-equivalent,
        // Image, Button and Text. If the caller has not selected a Canvas subtree, make the
        // required Canvas first so no UI element is ever orphaned outside a physical 3DS screen.
        Entity CreateUIButton(EditorContext& ctx, Screen screen, Entity parent = {}) {
            Entity canvas = FindCanvasAncestor(parent);
            if (!canvas)
                canvas = CreateCanvas(ctx, screen, parent);

            Entity button = ctx.SceneRef.CreateEntity("Button");
            auto& rect = button.AddComponent<UIRectComponent>();
            rect.SizeDelta = { 120.0f, 40.0f };
            button.AddComponent<UIImageComponent>();
            button.AddComponent<UIButtonComponent>();
            auto& text = button.AddComponent<UITextComponent>();
            text.Text = "Button";
            text.Alignment = TextAlignment::Center;
            ctx.SceneRef.SetParent(button, canvas);
            ctx.Selected = button;
            MarkSceneDirty(ctx);
            return button;
        }

        void DrawCreateObjectMenu(EditorContext& ctx, Entity parent = {}) {
            if (ImGui::MenuItem(parent ? "Create Empty Child" : "Create Empty"))
                CreateEmpty(ctx, parent);

            if (ImGui::BeginMenu("2D Object")) {
                if (ImGui::MenuItem("Sprite"))
                    CreateSprite(ctx, parent);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("3D Object")) {
                if (ImGui::MenuItem("Cube"))
                    CreatePrimitive(ctx, "Cube", MeshPrimitive::Cube, parent);
                if (ImGui::MenuItem("Sphere"))
                    CreatePrimitive(ctx, "Sphere", MeshPrimitive::Sphere, parent);
                if (ImGui::MenuItem("Capsule"))
                    CreatePrimitive(ctx, "Capsule", MeshPrimitive::Capsule, parent);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("UI")) {
                if (ImGui::MenuItem("Canvas (Top Screen)"))
                    CreateCanvas(ctx, Screen::Top, parent);
                if (ImGui::MenuItem("Canvas (Bottom Screen)"))
                    CreateCanvas(ctx, Screen::Bottom, parent);
                ImGui::Separator();
                if (ImGui::MenuItem("Button (Top Screen)"))
                    CreateUIButton(ctx, Screen::Top, parent);
                if (ImGui::MenuItem("Button (Bottom Screen)"))
                    CreateUIButton(ctx, Screen::Bottom, parent);
                ImGui::EndMenu();
            }
            if (!parent && ImGui::BeginMenu("3DS Screen Roots")) {
                if (ImGui::MenuItem("Bootstrap 3DS Scene (Roots + Cameras)"))
                    Bootstrap3DSScene(ctx);
                if (ImGui::MenuItem("Create Top and Bottom Roots"))
                    EnsureScreenRoots(ctx);
                if (ImGui::MenuItem("Organize Existing Screen Roots"))
                    OrganizeExistingScreenRoots(ctx);
                ImGui::EndMenu();
            }
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

        bool IsInSubtree(Scene& scene, Entity entity, Entity root) {
            while (entity && scene.Registry().valid(entity.Handle())) {
                if (entity == root)
                    return true;
                entity = entity.GetComponent<HierarchyComponent>().Parent;
            }
            return false;
        }

        // A search box over a tree with drag-drop reparenting either has to hide/show
        // whole subtrees (auto-expanding ancestors of any match) or fall back to a
        // simpler flat list while searching -- this takes the flat-list route: while the
        // search box has text, every matching entity in the WHOLE scene is listed (no
        // tree structure, no drag-drop), still clickable to select. Empty search box
        // shows the normal full tree UI, completely unchanged.
        void DrawFilteredFlatList(EditorContext& ctx, const std::string& filter, Entity& pendingRemoval) {
            for (auto handle : ctx.SceneRef.Registry().view<NameComponent>()) {
                Entity entity(handle, &ctx.SceneRef);
                const std::string& name = entity.GetComponent<NameComponent>().Name;
                if (!ContainsCaseInsensitive(name, filter))
                    continue;

                ImGui::PushID(EntityId(entity));
                bool selected = (entity == ctx.Selected);
                if (ImGui::Selectable(name.c_str(), selected))
                    ctx.Selected = entity;
                if (ImGui::BeginPopupContextItem()) {
                    DrawCreateObjectMenu(ctx, entity);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove", "Delete"))
                        pendingRemoval = entity;
                    ImGui::EndPopup();
                }
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

        void DrawEntityNode(Entity entity, EditorContext& ctx, Entity& pendingRemoval) {
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
            if (entity.HasComponent<LayerComponent>()) {
                Layer layer = entity.GetComponent<LayerComponent>().Value;
                ImVec4 color = (layer == Layer::TOP) ? ImVec4(0.3f, 0.9f, 0.9f, 1.0f) : ImVec4(0.95f, 0.6f, 0.2f, 1.0f);
                if (layer == Layer::Default)
                    color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                ImGui::ColorButton("##LayerMarker", color,
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
                DrawCreateObjectMenu(ctx, entity);
                ImGui::Separator();
                if (ImGui::MenuItem("Remove", "Delete"))
                    pendingRemoval = entity;
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
                    DrawEntityNode(child, ctx, pendingRemoval);
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

        if (ImGui::Button("Create Entity", ImVec2(-1, 0))) {
            ctx.Selected = ctx.SceneRef.CreateEntity("Entity");
            MarkSceneDirty(ctx);
        }

        ImGui::InputTextWithHint("##HierarchySearch", "Search...", m_SearchBuffer, sizeof(m_SearchBuffer));

        ImGui::Separator();

        // While searching, show a flat filtered list instead of the normal tree --
        // see DrawFilteredFlatList's own comment for why (a tree with drag-drop
        // reparenting doesn't have an obvious cheap way to hide/show whole subtrees).
        if (m_SearchBuffer[0] != '\0') {
            DrawFilteredFlatList(ctx, m_SearchBuffer, m_PendingRemoval);
        } else {
            // One real tree, not three display-only sections. Drop an entity onto
            // Top or Bottom to make it a child and inherit that screen's layer.
            // Copy protects this traversal from a drag-drop reparenting mutation.
            std::vector<Entity> roots = ctx.SceneRef.GetRootEntities();
            for (Entity entity : roots)
                DrawEntityNode(entity, ctx, m_PendingRemoval);

        // Drop target filling the remaining panel space below the tree -- it moves
        // an entity back to the unlayered scene root. InvisibleButton
        // asserts on a zero-size axis, which GetContentRegionAvail() can return
        // when the tree already fills the panel -- clamp to a 1px minimum.
        ImVec2 dropZoneSize = ImGui::GetContentRegionAvail();
        dropZoneSize.x = std::max(dropZoneSize.x, 1.0f);
        dropZoneSize.y = std::max(dropZoneSize.y, 1.0f);
        ImGui::InvisibleButton("##RootDropZone", dropZoneSize);
        if (ImGui::BeginDragDropTarget()) {
            AcceptReparentDrop(ctx, Entity{});
            // A Prefab asset dropped here (not an existing HIERARCHY_ENTITY drag)
            // instantiates a new copy at the scene root -- PrefabSerializer::Instantiate
            // itself just logs an error and returns an empty Entity if the dropped
            // asset isn't actually a valid prefab file, so no extension check needed
            // here first.
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_GUID")) {
                std::string guid = static_cast<const char*>(payload->Data);
                std::string path = AssetDatabase::ResolvePath(guid);
                if (!path.empty()) {
                    ctx.Selected = PrefabSerializer::Instantiate(ctx.SceneRef, path);
                    MarkSceneDirty(ctx);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Right-click anywhere in the panel (including empty space below the
        // list) for a Unity-style "Create Empty" context menu. NoOpenOverItems
        // lets each node's own BeginPopupContextItem (Create Child Entity) take
        // precedence when right-clicking directly on a row.
        if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            DrawCreateObjectMenu(ctx);
            ImGui::EndPopup();
        }
        }

        // Only react while this panel has keyboard focus, and never turn a
        // Delete keypress intended for the search box into an entity removal.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
            ctx.Selected && ctx.SceneRef.Registry().valid(ctx.Selected.Handle())) {
            m_PendingRemoval = ctx.Selected;
        }

        // Unity-style quick rename. A modal avoids changing the tree row's ImGui ID/layout in
        // the middle of traversal, and also works when the selection came from Scene or Game.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2) &&
            ctx.Selected && ctx.SceneRef.Registry().valid(ctx.Selected.Handle())) {
            m_Renaming = ctx.Selected;
            std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s",
                m_Renaming.GetComponent<NameComponent>().Name.c_str());
            ImGui::OpenPopup("Rename Entity");
        }

        if (m_Renaming && !ctx.SceneRef.Registry().valid(m_Renaming.Handle()))
            m_Renaming = Entity{};
        if (ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            bool submitted = ImGui::InputText("Name", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            if (submitted || ImGui::Button("Rename", ImVec2(100.0f, 0.0f))) {
                if (m_Renaming && ctx.SceneRef.Registry().valid(m_Renaming.Handle())) {
                    m_Renaming.GetComponent<NameComponent>().Name = m_RenameBuffer;
                    MarkSceneDirty(ctx);
                }
                m_Renaming = Entity{};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                m_Renaming = Entity{};
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_PendingRemoval && ctx.SceneRef.Registry().valid(m_PendingRemoval.Handle())) {
            if (IsInSubtree(ctx.SceneRef, ctx.Selected, m_PendingRemoval))
                ctx.Selected = Entity{};
            ctx.SceneRef.DestroyEntity(m_PendingRemoval);
            MarkSceneDirty(ctx);
        }
        m_PendingRemoval = Entity{};

        ImGui::End();
    }

}
