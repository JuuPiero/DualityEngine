#include "DualityEditor/Panels/HierarchyPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
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

        const char* IconForEntity(Entity entity) {
            if (entity.HasComponent<CameraComponent>())
                return EditorIcons::Camera;
            if (entity.HasComponent<CanvasComponent>())
                return EditorIcons::Package;
            if (entity.HasComponent<SpriteRendererComponent>() || entity.HasComponent<UIImageComponent>())
                return EditorIcons::Image;
            if (entity.HasComponent<MeshRendererComponent>())
                return EditorIcons::Cube;
            if (entity.HasComponent<BehaviourComponent>())
                return EditorIcons::Code;
            return EditorIcons::File;
        }

        Entity DraggedHierarchyEntity(EditorContext& ctx) {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (!payload || std::strcmp(payload->DataType, "HIERARCHY_ENTITY") != 0 ||
                payload->DataSize != sizeof(entt::entity))
                return {};
            entt::entity handle = *static_cast<const entt::entity*>(payload->Data);
            return ctx.SceneRef.Registry().valid(handle) ? Entity(handle, &ctx.SceneRef) : Entity{};
        }

        bool CanReparent(Entity child, Entity parent) {
            if (!child || child == parent)
                return false;
            // A child cannot be made a descendant of itself. Keep this UI-side
            // guard in addition to Scene::SetParent's own defensive check so an
            // invalid target never advertises itself as a valid blue drop zone.
            for (Entity current = parent; current; current = current.GetComponent<HierarchyComponent>().Parent) {
                if (current == child)
                    return false;
            }
            return true;
        }

        const std::vector<Entity>& SiblingsFor(const Scene& scene, Entity parent) {
            return parent ? parent.GetComponent<HierarchyComponent>().Children : scene.GetRootEntities();
        }

        size_t SiblingIndexOf(const std::vector<Entity>& siblings, Entity entity) {
            auto it = std::find(siblings.begin(), siblings.end(), entity);
            return it == siblings.end() ? siblings.size() : static_cast<size_t>(it - siblings.begin());
        }

        void SetSiblingIndex(EditorContext& ctx, Entity dragged, Entity parent, size_t index) {
            if (!CanReparent(dragged, parent))
                return;
            const std::vector<Entity>& siblings = SiblingsFor(ctx.SceneRef, parent);
            Entity oldParent = dragged.GetComponent<HierarchyComponent>().Parent;
            size_t oldIndex = oldParent == parent ? SiblingIndexOf(siblings, dragged) : siblings.size();
            // The requested index is measured before SetSiblingIndex removes the
            // dragged row. Account for that removal so dropping B below C gives
            // A/C/B rather than A/C/D/B.
            if (oldParent == parent && oldIndex < index)
                --index;
            if (oldParent == parent && oldIndex == index)
                return;
            ctx.SceneRef.SetSiblingIndex(dragged, parent, index);
            MarkSceneDirty(ctx);
        }

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
                if (payload->Delivery && ctx.SceneRef.Registry().valid(draggedHandle) && CanReparent(dragged, newParent)) {
                    ctx.SceneRef.SetParent(dragged, newParent, insertAfter);
                    MarkSceneDirty(ctx);
                }
            }
        }

        enum class RowDropPlacement { Before, Child, After };

        void AcceptEntityRowDrop(EditorContext& ctx, Entity target) {
            Entity dragged = DraggedHierarchyEntity(ctx);
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            float edge = (max.y - min.y) * 0.25f;
            float mouseY = ImGui::GetIO().MousePos.y;
            RowDropPlacement placement = mouseY < min.y + edge ? RowDropPlacement::Before :
                (mouseY > max.y - edge ? RowDropPlacement::After : RowDropPlacement::Child);

            Entity parent = placement == RowDropPlacement::Child
                ? target : target.GetComponent<HierarchyComponent>().Parent;
            bool valid = dragged && dragged != target && CanReparent(dragged, parent);
            if (valid) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImU32 highlight = IM_COL32(80, 160, 255, 255);
                if (placement == RowDropPlacement::Child)
                    drawList->AddRect(min, max, highlight, 2.0f, 0, 2.0f);
                else {
                    float y = placement == RowDropPlacement::Before ? min.y : max.y;
                    drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), highlight, 2.0f);
                }
            }

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                if (payload->Delivery && valid) {
                    if (placement == RowDropPlacement::Child) {
                        ctx.SceneRef.SetParent(dragged, target);
                        MarkSceneDirty(ctx);
                    } else {
                        const std::vector<Entity>& siblings = SiblingsFor(ctx.SceneRef, parent);
                        size_t targetIndex = SiblingIndexOf(siblings, target);
                        SetSiblingIndex(ctx, dragged, parent,
                            placement == RowDropPlacement::Before ? targetIndex : targetIndex + 1);
                    }
                }
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

        // Cameras created from Hierarchy are ready for the pointer system immediately.
        // Keep one primary camera per physical screen by making additional cameras
        // non-primary; the user can still promote one explicitly in the Inspector.
        Entity CreateCamera(EditorContext& ctx, Screen screen, ProjectionType projection, Entity parent = {}) {
            if (!parent)
                parent = ctx.SceneRef.EnsureScreenRoot(screen);

            const bool is3D = projection == ProjectionType::Perspective;
            Entity camera = ctx.SceneRef.CreateEntity((screen == Screen::Top ? "Top " : "Bottom ")
                + std::string(is3D ? "3D Camera" : "2D Camera"));
            auto& component = camera.AddComponent<CameraComponent>();
            component.Screen = screen;
            component.Projection = projection;
            component.Primary = !ctx.SceneRef.GetPrimaryCamera(screen);
            if (is3D) {
                // A practical 3D starting point for the engine's -Z-forward camera.
                camera.GetComponent<TransformComponent>().Translation.z = 200.0f;
                camera.AddComponent<PhysicsRaycaster3DComponent>();
            } else {
                camera.AddComponent<PhysicsRaycaster2DComponent>();
            }
            ctx.SceneRef.SetParent(camera, parent);
            ctx.Selected = camera;
            MarkSceneDirty(ctx);
            return camera;
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

        Entity CreateLineRenderer(EditorContext& ctx, Entity parent = {}) {
            Entity line = ctx.SceneRef.CreateEntity("Line Renderer");
            line.AddComponent<LineRendererComponent>();
            if (parent)
                ctx.SceneRef.SetParent(line, parent);
            ctx.Selected = line;
            MarkSceneDirty(ctx);
            return line;
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

        Entity EnsureCanvasForUI(EditorContext& ctx, Screen screen, Entity parent) {
            Entity canvas = FindCanvasAncestor(parent);
            return canvas ? canvas : CreateCanvas(ctx, screen, parent);
        }

        Entity CreateUIElement(EditorContext& ctx, const char* name, Screen screen, Entity parent,
                               bool image, const char* text = nullptr) {
            Entity canvas = EnsureCanvasForUI(ctx, screen, parent);
            Entity element = ctx.SceneRef.CreateEntity(name);
            element.AddComponent<UIRectComponent>();
            if (image)
                element.AddComponent<UIImageComponent>();
            if (text) {
                auto& textComponent = element.AddComponent<UITextComponent>();
                textComponent.Text = text;
                textComponent.Alignment = TextAlignment::Center;
            }
            ctx.SceneRef.SetParent(element, canvas);
            ctx.Selected = element;
            MarkSceneDirty(ctx);
            return element;
        }

        // Creates Unity's familiar Button bundle in one action: RectTransform-equivalent,
        // Image, Button and Text. If the caller has not selected a Canvas subtree, make the
        // required Canvas first so no UI element is ever orphaned outside a physical 3DS screen.
        Entity CreateUIButton(EditorContext& ctx, Screen screen, Entity parent = {}) {
            Entity button = CreateUIElement(ctx, "Button", screen, parent, true, "Button");
            auto& rect = button.GetComponent<UIRectComponent>();
            rect.SizeDelta = { 120.0f, 40.0f };
            button.AddComponent<UIButtonComponent>();
            return button;
        }

        Entity CreateUISlider(EditorContext& ctx, Screen screen, Entity parent = {}) {
            Entity slider = CreateUIElement(ctx, "Slider", screen, parent, true);
            slider.GetComponent<UIRectComponent>().SizeDelta = { 160.0f, 20.0f };
            slider.AddComponent<UISliderComponent>();
            return slider;
        }

        Entity CreateUIToggle(EditorContext& ctx, Screen screen, Entity parent = {}) {
            Entity toggle = CreateUIElement(ctx, "Toggle", screen, parent, true);
            toggle.GetComponent<UIRectComponent>().SizeDelta = { 24.0f, 24.0f };
            toggle.AddComponent<UIToggleComponent>();
            return toggle;
        }

        Entity CreateUIInputField(EditorContext& ctx, Screen screen, Entity parent = {}) {
            Entity field = CreateUIElement(ctx, "Input Field", screen, parent, true);
            field.GetComponent<UIRectComponent>().SizeDelta = { 160.0f, 28.0f };
            field.AddComponent<UIInputFieldComponent>();
            return field;
        }

        void DrawCreateObjectMenu(EditorContext& ctx, Entity parent = {}) {
            if (ImGui::MenuItem(parent ? "Create Empty Child" : "Create Empty"))
                CreateEmpty(ctx, parent);

            if (ImGui::BeginMenu("Camera")) {
                if (ImGui::MenuItem("Top Screen 2D Camera"))
                    CreateCamera(ctx, Screen::Top, ProjectionType::Orthographic, parent);
                if (ImGui::MenuItem("Bottom Screen 2D Camera"))
                    CreateCamera(ctx, Screen::Bottom, ProjectionType::Orthographic, parent);
                ImGui::Separator();
                if (ImGui::MenuItem("Top Screen 3D Camera"))
                    CreateCamera(ctx, Screen::Top, ProjectionType::Perspective, parent);
                if (ImGui::MenuItem("Bottom Screen 3D Camera"))
                    CreateCamera(ctx, Screen::Bottom, ProjectionType::Perspective, parent);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("2D Object")) {
                if (ImGui::MenuItem("Sprite"))
                    CreateSprite(ctx, parent);
                if (ImGui::MenuItem("Line Renderer"))
                    CreateLineRenderer(ctx, parent);
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
                if (ImGui::BeginMenu("Top Screen")) {
                    if (ImGui::MenuItem("Panel")) CreateUIElement(ctx, "Panel", Screen::Top, parent, true);
                    if (ImGui::MenuItem("Image")) CreateUIElement(ctx, "Image", Screen::Top, parent, true);
                    if (ImGui::MenuItem("Text")) CreateUIElement(ctx, "Text", Screen::Top, parent, false, "Text");
                    if (ImGui::MenuItem("Button")) CreateUIButton(ctx, Screen::Top, parent);
                    if (ImGui::MenuItem("Slider")) CreateUISlider(ctx, Screen::Top, parent);
                    if (ImGui::MenuItem("Toggle")) CreateUIToggle(ctx, Screen::Top, parent);
                    if (ImGui::MenuItem("Input Field")) CreateUIInputField(ctx, Screen::Top, parent);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Bottom Screen")) {
                    if (ImGui::MenuItem("Panel")) CreateUIElement(ctx, "Panel", Screen::Bottom, parent, true);
                    if (ImGui::MenuItem("Image")) CreateUIElement(ctx, "Image", Screen::Bottom, parent, true);
                    if (ImGui::MenuItem("Text")) CreateUIElement(ctx, "Text", Screen::Bottom, parent, false, "Text");
                    if (ImGui::MenuItem("Button")) CreateUIButton(ctx, Screen::Bottom, parent);
                    if (ImGui::MenuItem("Slider")) CreateUISlider(ctx, Screen::Bottom, parent);
                    if (ImGui::MenuItem("Toggle")) CreateUIToggle(ctx, Screen::Bottom, parent);
                    if (ImGui::MenuItem("Input Field")) CreateUIInputField(ctx, Screen::Bottom, parent);
                    ImGui::EndMenu();
                }
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

        bool IsSelected(const EditorContext& ctx, Entity entity) {
            return std::find(ctx.SelectedEntities.begin(), ctx.SelectedEntities.end(), entity) != ctx.SelectedEntities.end();
        }

        void NormalizeSelection(EditorContext& ctx, Entity& rangeAnchor) {
            auto& selection = ctx.SelectedEntities;
            selection.erase(std::remove_if(selection.begin(), selection.end(), [&](Entity entity) {
                return !entity || !ctx.SceneRef.Registry().valid(entity.Handle());
            }), selection.end());
            if (!ctx.Selected || !ctx.SceneRef.Registry().valid(ctx.Selected.Handle())) {
                ctx.Selected = Entity{};
                selection.clear();
                rangeAnchor = Entity{};
                return;
            }
            if (!IsSelected(ctx, ctx.Selected)) {
                selection = { ctx.Selected };
                rangeAnchor = ctx.Selected;
            }
        }

        void SelectEntity(EditorContext& ctx, Entity entity, Entity& rangeAnchor, bool additive, bool range) {
            if (!entity || !ctx.SceneRef.Registry().valid(entity.Handle()))
                return;

            auto& selection = ctx.SelectedEntities;
            if (range && rangeAnchor && ctx.SceneRef.Registry().valid(rangeAnchor.Handle())) {
                const std::vector<Entity> traversal = ctx.SceneRef.GetHierarchyTraversalOrder();
                auto first = std::find(traversal.begin(), traversal.end(), rangeAnchor);
                auto last = std::find(traversal.begin(), traversal.end(), entity);
                if (first != traversal.end() && last != traversal.end()) {
                    if (first > last)
                        std::swap(first, last);
                    selection.assign(first, last + 1);
                } else {
                    selection = { entity };
                    rangeAnchor = entity;
                }
            } else if (additive) {
                auto found = std::find(selection.begin(), selection.end(), entity);
                if (found == selection.end())
                    selection.push_back(entity);
                else
                    selection.erase(found);
                if (!rangeAnchor)
                    rangeAnchor = entity;
            } else {
                selection = { entity };
                rangeAnchor = entity;
            }

            ctx.Selected = selection.empty() ? Entity{} : (IsSelected(ctx, entity) ? entity : selection.back());
            ctx.SelectedAssetPath.clear();
            ctx.SelectedAssetPaths.clear();
        }

        std::vector<Entity> TopLevelSelection(EditorContext& ctx) {
            std::vector<Entity> roots;
            for (Entity entity : ctx.SelectedEntities) {
                if (!entity || !ctx.SceneRef.Registry().valid(entity.Handle()))
                    continue;
                bool ancestorSelected = false;
                for (Entity parent = entity.GetComponent<HierarchyComponent>().Parent; parent;
                     parent = parent.GetComponent<HierarchyComponent>().Parent) {
                    if (IsSelected(ctx, parent)) {
                        ancestorSelected = true;
                        break;
                    }
                }
                if (!ancestorSelected)
                    roots.push_back(entity);
            }
            return roots;
        }

        void DuplicateSelection(EditorContext& ctx, Entity& rangeAnchor) {
            std::vector<Entity> duplicates;
            for (Entity source : TopLevelSelection(ctx)) {
                Entity duplicate = PrefabSerializer::Duplicate(ctx.SceneRef, source);
                if (duplicate)
                    duplicates.push_back(duplicate);
            }
            if (duplicates.empty())
                return;
            ctx.SelectedEntities = duplicates;
            ctx.Selected = duplicates.back();
            ctx.SelectedAssetPath.clear();
            ctx.SelectedAssetPaths.clear();
            rangeAnchor = ctx.Selected;
            MarkSceneDirty(ctx);
        }

        // A search box over a tree with drag-drop reparenting either has to hide/show
        // whole subtrees (auto-expanding ancestors of any match) or fall back to a
        // simpler flat list while searching -- this takes the flat-list route: while the
        // search box has text, every matching entity in the WHOLE scene is listed (no
        // tree structure, no drag-drop), still clickable to select. Empty search box
        // shows the normal full tree UI, completely unchanged.
        void DrawFilteredFlatList(EditorContext& ctx, const std::string& filter, std::vector<Entity>& pendingRemovals, Entity& rangeAnchor) {
            for (auto handle : ctx.SceneRef.Registry().view<NameComponent>()) {
                Entity entity(handle, &ctx.SceneRef);
                const std::string& name = entity.GetComponent<NameComponent>().Name;
                if (!ContainsCaseInsensitive(name, filter))
                    continue;

                ImGui::PushID(EntityId(entity));
                bool selected = IsSelected(ctx, entity);
                const std::string labelledName = std::string(IconForEntity(entity)) + " " + name;
                if (ImGui::Selectable(labelledName.c_str(), selected))
                    SelectEntity(ctx, entity, rangeAnchor, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected)
                    SelectEntity(ctx, entity, rangeAnchor, false, false);
                if (ImGui::BeginPopupContextItem()) {
                    DrawCreateObjectMenu(ctx, entity);
                    ImGui::Separator();
                    if (ImGui::MenuItem((std::string(EditorIcons::Copy) + " Duplicate").c_str(), "Ctrl+D"))
                        DuplicateSelection(ctx, rangeAnchor);
                    if (ImGui::MenuItem((std::string(EditorIcons::Trash) + " Remove").c_str(), "Delete"))
                        pendingRemovals = TopLevelSelection(ctx);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }

        void DrawEntityNode(Entity entity, EditorContext& ctx, std::vector<Entity>& pendingRemovals, Entity& rangeAnchor) {
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
            const std::string labelledName = std::string(IconForEntity(entity)) + " " + name;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (IsSelected(ctx, entity))
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

            bool open = ImGui::TreeNodeEx(labelledName.c_str(), flags);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                SelectEntity(ctx, entity, rangeAnchor, ImGui::GetIO().KeyCtrl, ImGui::GetIO().KeyShift);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsSelected(ctx, entity))
                SelectEntity(ctx, entity, rangeAnchor, false, false);

            if (ImGui::BeginDragDropSource()) {
                if (!IsSelected(ctx, entity))
                    SelectEntity(ctx, entity, rangeAnchor, false, false);
                entt::entity handle = entity.Handle();
                ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &handle, sizeof(handle));
                ImGui::Text("%s", labelledName.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                // Top quarter = before this sibling, middle = child, bottom
                // quarter = after this sibling. This makes reordering possible
                // directly on a row instead of requiring a 4px invisible gap.
                AcceptEntityRowDrop(ctx, entity);
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem()) {
                DrawCreateObjectMenu(ctx, entity);
                ImGui::Separator();
                if (ImGui::MenuItem((std::string(EditorIcons::Copy) + " Duplicate").c_str(), "Ctrl+D"))
                    DuplicateSelection(ctx, rangeAnchor);
                if (ImGui::MenuItem((std::string(EditorIcons::Trash) + " Remove").c_str(), "Delete"))
                    pendingRemovals = TopLevelSelection(ctx);
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
                    DrawEntityNode(child, ctx, pendingRemovals, rangeAnchor);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void HierarchyPanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Hierarchy");
        NormalizeSelection(ctx, m_RangeAnchor);

        if (ImGui::Button((std::string(EditorIcons::Add) + " Create Entity").c_str(), ImVec2(-1, 0))) {
            SelectEntity(ctx, ctx.SceneRef.CreateEntity("Entity"), m_RangeAnchor, false, false);
            MarkSceneDirty(ctx);
        }

        ImGui::InputTextWithHint("##HierarchySearch", "Search...", m_SearchBuffer, sizeof(m_SearchBuffer));

        ImGui::Separator();

        // While searching, show a flat filtered list instead of the normal tree --
        // see DrawFilteredFlatList's own comment for why (a tree with drag-drop
        // reparenting doesn't have an obvious cheap way to hide/show whole subtrees).
        if (m_SearchBuffer[0] != '\0') {
            DrawFilteredFlatList(ctx, m_SearchBuffer, m_PendingRemovals, m_RangeAnchor);
        } else {
            // One real tree, not three display-only sections. Drop an entity onto
            // Top or Bottom to make it a child and inherit that screen's layer.
            // Copy protects this traversal from a drag-drop reparenting mutation.
            std::vector<Entity> roots = ctx.SceneRef.GetRootEntities();
            for (Entity entity : roots)
                DrawEntityNode(entity, ctx, m_PendingRemovals, m_RangeAnchor);

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
                    Entity instance = PrefabSerializer::Instantiate(ctx.SceneRef, path);
                    if (instance) {
                        SelectEntity(ctx, instance, m_RangeAnchor, false, false);
                        MarkSceneDirty(ctx);
                    }
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

        // Scope scene-object shortcuts to this window; typing in the search box
        // must not turn Ctrl+D/Delete into a scene mutation.
        if (!ctx.IsPlaying && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_D, false) && !ctx.SelectedEntities.empty())
            DuplicateSelection(ctx, m_RangeAnchor);

        // Only react while this panel has keyboard focus, and never turn a
        // Delete keypress intended for the search box into an entity removal.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
            !ctx.SelectedEntities.empty()) {
            m_PendingRemovals = TopLevelSelection(ctx);
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

        bool removedAny = false;
        for (Entity pendingRemoval : m_PendingRemovals) {
            if (!pendingRemoval || !ctx.SceneRef.Registry().valid(pendingRemoval.Handle()))
                continue;
            ctx.SceneRef.DestroyEntity(pendingRemoval);
            removedAny = true;
        }
        if (removedAny) {
            ctx.SelectedEntities.erase(std::remove_if(ctx.SelectedEntities.begin(), ctx.SelectedEntities.end(), [&](Entity entity) {
                return !entity || !ctx.SceneRef.Registry().valid(entity.Handle());
            }), ctx.SelectedEntities.end());
            ctx.Selected = ctx.SelectedEntities.empty() ? Entity{} : ctx.SelectedEntities.back();
            m_RangeAnchor = ctx.Selected;
            MarkSceneDirty(ctx);
        }
        m_PendingRemovals.clear();

        ImGui::End();
    }

}
