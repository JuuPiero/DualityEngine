#include "DualityEditor/EditorCommands.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/Windows/AnimationWindow.h"
#include "DualityEditor/Windows/ProfilerWindow.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality::EditorCommands {

    namespace {
        bool IsLive(const EditorContext& ctx, Entity entity) {
            return entity && entity.GetScene() == &ctx.SceneRef && ctx.SceneRef.Registry().valid(entity.Handle());
        }

        void RevealAndPing(EditorContext& ctx) {
            ctx.Session.RequestRevealSelection = true;
        }
    }

    // ------------------------------------------------------------------------------ Selection

    bool IsSelected(const EditorContext& ctx, Entity entity) {
        return std::find(ctx.SelectedEntities.begin(), ctx.SelectedEntities.end(), entity) != ctx.SelectedEntities.end();
    }

    void SelectOnly(EditorContext& ctx, Entity entity) {
        if (!IsLive(ctx, entity)) {
            ClearSelection(ctx);
            return;
        }
        ctx.SelectedEntities = { entity };
        ctx.Selected = entity;
        ctx.SelectedAssetPath.clear();
        ctx.SelectedAssetPaths.clear();
    }

    void SetSelection(EditorContext& ctx, const std::vector<Entity>& entities, Entity primary) {
        ctx.SelectedEntities.clear();
        for (Entity entity : entities) {
            if (IsLive(ctx, entity) && !IsSelected(ctx, entity))
                ctx.SelectedEntities.push_back(entity);
        }
        if (IsLive(ctx, primary) && IsSelected(ctx, primary))
            ctx.Selected = primary;
        else
            ctx.Selected = ctx.SelectedEntities.empty() ? Entity{} : ctx.SelectedEntities.back();
        if (!ctx.SelectedEntities.empty()) {
            ctx.SelectedAssetPath.clear();
            ctx.SelectedAssetPaths.clear();
        }
    }

    void ToggleSelected(EditorContext& ctx, Entity entity) {
        if (!IsLive(ctx, entity))
            return;
        auto found = std::find(ctx.SelectedEntities.begin(), ctx.SelectedEntities.end(), entity);
        if (found != ctx.SelectedEntities.end()) {
            ctx.SelectedEntities.erase(found);
            if (ctx.Selected == entity)
                ctx.Selected = ctx.SelectedEntities.empty() ? Entity{} : ctx.SelectedEntities.back();
        } else {
            ctx.SelectedEntities.push_back(entity);
            ctx.Selected = entity;
        }
        ctx.SelectedAssetPath.clear();
        ctx.SelectedAssetPaths.clear();
    }

    void ClearSelection(EditorContext& ctx) {
        ctx.Selected = Entity{};
        ctx.SelectedEntities.clear();
    }

    void SelectAll(EditorContext& ctx) {
        SetSelection(ctx, ctx.SceneRef.GetHierarchyTraversalOrder(), ctx.Selected);
    }

    void NormalizeSelection(EditorContext& ctx) {
        auto& selection = ctx.SelectedEntities;
        selection.erase(std::remove_if(selection.begin(), selection.end(), [&](Entity entity) {
            return !IsLive(ctx, entity);
        }), selection.end());
        if (!IsLive(ctx, ctx.Selected)) {
            ctx.Selected = selection.empty() ? Entity{} : selection.back();
            return;
        }
        // Code that only assigns ctx.Selected (older call sites, Game view UI picking) means
        // "select just this".
        if (!IsSelected(ctx, ctx.Selected))
            selection = { ctx.Selected };
    }

    std::vector<Entity> TopLevelSelection(EditorContext& ctx) {
        std::vector<Entity> roots;
        for (Entity entity : ctx.SceneRef.GetHierarchyTraversalOrder()) {
            if (!IsSelected(ctx, entity))
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

    HistorySelection CaptureSelection(EditorContext& ctx) {
        HistorySelection selection;
        selection.Entities = ctx.Session.Ids.IdsOf(ctx.SelectedEntities);
        selection.Primary = ctx.Session.Ids.Get(ctx.Selected);
        return selection;
    }

    void ApplySelection(EditorContext& ctx, const HistorySelection& selection) {
        std::vector<Entity> entities = ctx.Session.Ids.Resolve(ctx.SceneRef, selection.Entities);
        Entity primary = ctx.Session.Ids.Find(ctx.SceneRef, selection.Primary);
        ctx.SelectedEntities.clear();
        ctx.Selected = Entity{};
        SetSelection(ctx, entities, primary);
    }

    // ------------------------------------------------------------------------- Entity commands

    void DuplicateSelection(EditorContext& ctx) {
        std::vector<Entity> duplicates;
        for (Entity source : TopLevelSelection(ctx)) {
            if (Entity duplicate = PrefabSerializer::Duplicate(ctx.SceneRef, source))
                duplicates.push_back(duplicate);
        }
        if (duplicates.empty())
            return;
        SetSelection(ctx, duplicates, duplicates.back());
        MarkSceneDirty(ctx, "Duplicate");
        RevealAndPing(ctx);
    }

    void DeleteSelection(EditorContext& ctx) {
        const std::vector<Entity> roots = TopLevelSelection(ctx);
        if (roots.empty())
            return;
        for (Entity entity : roots) {
            if (IsLive(ctx, entity))
                ctx.SceneRef.DestroyEntity(entity);
        }
        ClearSelection(ctx);
        ctx.DraggingGizmoAxis = GizmoAxis::None;
        MarkSceneDirty(ctx, roots.size() == 1 ? "Delete" : "Delete Objects");
    }

    namespace {
        void CollectRawHandles(const nlohmann::json& value, std::unordered_set<uint32_t>& out) {
            if (value.is_object()) {
                if (value.size() == 1 && value.contains("$external") && value["$external"].is_number_unsigned()) {
                    out.insert(value["$external"].get<uint32_t>());
                    return;
                }
                for (const auto& item : value.items())
                    CollectRawHandles(item.value(), out);
            } else if (value.is_array()) {
                for (const auto& item : value)
                    CollectRawHandles(item, out);
            }
        }
    }

    void CopySelection(EditorContext& ctx) {
        const std::vector<Entity> roots = TopLevelSelection(ctx);
        if (roots.empty())
            return;
        nlohmann::json clipboard = PrefabSerializer::CopyToJson(ctx.SceneRef, roots);
        std::unordered_set<uint32_t> handles;
        CollectRawHandles(clipboard["Entities"], handles);
        for (const auto& root : clipboard["Roots"]) {
            const uint32_t parent = root.value("SourceParent", EntityRef::Invalid);
            if (parent != EntityRef::Invalid)
                handles.insert(parent);
        }
        ctx.Session.ClipboardHandleIds.clear();
        for (uint32_t handle : handles) {
            const entt::entity entity = static_cast<entt::entity>(handle);
            if (ctx.SceneRef.Registry().valid(entity))
                ctx.Session.ClipboardHandleIds[handle] = ctx.Session.Ids.Get(Entity(entity, &ctx.SceneRef));
        }
        ctx.Session.ClipboardJson = clipboard.dump();
        // Mirror to the OS clipboard as well, so a copied hierarchy can be inspected/shared.
        ImGui::SetClipboardText(ctx.Session.ClipboardJson.c_str());
    }

    void CutSelection(EditorContext& ctx) {
        CopySelection(ctx);
        if (HasClipboard(ctx)) {
            DeleteSelection(ctx);
            ctx.Session.PendingUndoLabel = "Cut";
        }
    }

    bool HasClipboard(const EditorContext& ctx) {
        return !ctx.Session.ClipboardJson.empty();
    }

    void Paste(EditorContext& ctx, bool asChild) {
        if (!HasClipboard(ctx))
            return;
        nlohmann::json clipboard = nlohmann::json::parse(ctx.Session.ClipboardJson, nullptr, false);
        if (clipboard.is_discarded())
            return;

        // Handles recorded at copy time -> handles of the current scene generation (Undo/Redo
        // and Play/Stop rebuild every handle; stable ids bridge them).
        std::unordered_map<uint32_t, uint32_t> remap;
        for (const auto& [handle, id] : ctx.Session.ClipboardHandleIds) {
            Entity now = ctx.Session.Ids.Find(ctx.SceneRef, id);
            remap[handle] = now ? static_cast<uint32_t>(now.Handle()) : EntityRef::Invalid;
        }

        Entity parent;
        if (asChild) {
            parent = ctx.Selected;
        } else if (clipboard.contains("Roots") && !clipboard["Roots"].empty()) {
            const uint32_t sourceParent = clipboard["Roots"][0].value("SourceParent", EntityRef::Invalid);
            auto mapped = remap.find(sourceParent);
            if (mapped != remap.end() && mapped->second != EntityRef::Invalid)
                parent = Entity(static_cast<entt::entity>(mapped->second), &ctx.SceneRef);
        }

        std::vector<Entity> pasted = PrefabSerializer::PasteFromJson(ctx.SceneRef, clipboard, IsLive(ctx, parent) ? parent : Entity{}, &remap);
        if (pasted.empty())
            return;
        SetSelection(ctx, pasted, pasted.back());
        MarkSceneDirty(ctx, asChild ? "Paste As Child" : "Paste");
        RevealAndPing(ctx);
    }

    Entity CreateEmpty(EditorContext& ctx, Entity parent) {
        Entity entity = ctx.SceneRef.CreateEntity("GameObject");
        if (IsLive(ctx, parent))
            ctx.SceneRef.SetParent(entity, parent, {}, /*preserveWorldPosition=*/false);
        SelectOnly(ctx, entity);
        MarkSceneDirty(ctx, parent ? "Create Empty Child" : "Create Empty");
        RevealAndPing(ctx);
        return entity;
    }

    Entity CreateEmptyParent(EditorContext& ctx) {
        const std::vector<Entity> roots = TopLevelSelection(ctx);
        if (roots.empty())
            return CreateEmpty(ctx);
        // Unity: the new parent takes the place of the first selected object.
        Entity first = roots.front();
        Entity oldParent = first.GetComponent<HierarchyComponent>().Parent;
        const std::vector<Entity>& siblings = oldParent ? oldParent.GetComponent<HierarchyComponent>().Children : ctx.SceneRef.GetRootEntities();
        const size_t index = static_cast<size_t>(std::find(siblings.begin(), siblings.end(), first) - siblings.begin());
        Entity group = ctx.SceneRef.CreateEntity("GameObject");
        ctx.SceneRef.SetSiblingIndex(group, oldParent, index, false);
        group.GetComponent<TransformComponent>().Translation = ctx.SceneRef.GetWorldTransform(first).Translation;
        if (oldParent) {
            TransformComponent local = ctx.SceneRef.WorldToLocal(group, ctx.SceneRef.GetWorldTransform(first));
            group.GetComponent<TransformComponent>().Translation = local.Translation;
        }
        for (Entity entity : roots)
            ctx.SceneRef.SetParent(entity, group, {}, /*preserveWorldPosition=*/true);
        SelectOnly(ctx, group);
        MarkSceneDirty(ctx, "Create Empty Parent");
        RevealAndPing(ctx);
        return group;
    }

    void SetActive(EditorContext& ctx, Entity entity, bool active) {
        if (!IsLive(ctx, entity))
            return;
        auto& component = entity.HasComponent<ActiveComponent>() ? entity.GetComponent<ActiveComponent>() : entity.AddComponent<ActiveComponent>();
        if (component.Active == active)
            return;
        component.Active = active;
        MarkSceneDirty(ctx, active ? "Activate" : "Deactivate");
    }

    void ToggleActiveSelection(EditorContext& ctx) {
        if (!IsLive(ctx, ctx.Selected))
            return;
        const bool active = !(ctx.Selected.HasComponent<ActiveComponent>() && ctx.Selected.GetComponent<ActiveComponent>().Active);
        for (Entity entity : ctx.SelectedEntities)
            SetActive(ctx, entity, active);
    }

    void CreatePrefab(EditorContext& ctx, Entity entity) {
        if (!IsLive(ctx, entity))
            return;
        std::filesystem::path assetsDir = std::filesystem::path(ctx.ScenePath).parent_path();
        if (auto project = Project::GetActive())
            assetsDir = project->GetAssetsDirectory();
        const std::filesystem::path prefabsDir = assetsDir / "Prefabs";
        std::filesystem::create_directories(prefabsDir);
        // Entity names may contain characters a file name cannot ("Cube (Lit/Specular)").
        std::string fileName = entity.GetComponent<NameComponent>().Name;
        for (char& c : fileName)
            if (std::string("\\/:*?\"<>|").find(c) != std::string::npos)
                c = '_';
        if (fileName.empty())
            fileName = "Prefab";
        const std::filesystem::path prefabPath = prefabsDir / (fileName + ".prefab");
        if (!PrefabSerializer::Save(entity, prefabPath.string()))
            return;
        const std::string guid = AssetMeta::EnsureMetaFile(prefabPath);
        AssetDatabase::Register(guid, prefabPath.string());
        if (entity.HasComponent<PrefabInstanceComponent>())
            entity.GetComponent<PrefabInstanceComponent>().Prefab = AssetRef{ guid };
        else
            entity.AddComponent<PrefabInstanceComponent>(PrefabInstanceComponent{ AssetRef{ guid } });
        MarkSceneDirty(ctx, "Create Prefab");
        ctx.Session.PingAssetPath = prefabPath.string();
        ctx.Session.PingStartTime = ImGui::GetTime();
    }

    // -------------------------------------------------------------------- Creation (GameObject)

    namespace {
        Entity Finish(EditorContext& ctx, Entity entity, Entity parent, const char* label) {
            if (IsLive(ctx, parent) && entity.GetComponent<HierarchyComponent>().Parent != parent)
                ctx.SceneRef.SetParent(entity, parent, {}, /*preserveWorldPosition=*/false);
            SelectOnly(ctx, entity);
            MarkSceneDirty(ctx, label);
            RevealAndPing(ctx);
            return entity;
        }

        Entity CreateCanvas(EditorContext& ctx, Screen screen, Entity parent = {}) {
            // Canvas belongs to a physical screen. With no explicit parent, put it beneath that
            // screen's organizational root so UI follows the one-tree Top/Bottom convention.
            if (!IsLive(ctx, parent))
                parent = ctx.SceneRef.EnsureScreenRoot(screen);
            Entity canvas = ctx.SceneRef.CreateEntity(screen == Screen::Top ? "Top Canvas" : "Bottom Canvas");
            canvas.AddComponent<CanvasComponent>().Screen = screen;
            return Finish(ctx, canvas, parent, "Create Canvas");
        }

        // Cameras created from the menu are ready for the pointer system immediately. Keep one
        // primary camera per physical screen by making additional cameras non-primary.
        Entity CreateCamera(EditorContext& ctx, Screen screen, ProjectionType projection, Entity parent = {}) {
            if (!IsLive(ctx, parent))
                parent = ctx.SceneRef.EnsureScreenRoot(screen);
            const bool is3D = projection == ProjectionType::Perspective;
            Entity camera = ctx.SceneRef.CreateEntity((screen == Screen::Top ? "Top " : "Bottom ") + std::string(is3D ? "3D Camera" : "2D Camera"));
            auto& component = camera.AddComponent<CameraComponent>();
            component.Screen = screen;
            component.Projection = projection;
            component.Primary = !ctx.SceneRef.GetPrimaryCamera(screen);
            if (is3D) {
                camera.GetComponent<TransformComponent>().Translation.z = 10.0f;
                camera.AddComponent<PhysicsRaycaster3DComponent>();
            } else {
                camera.AddComponent<PhysicsRaycaster2DComponent>();
            }
            return Finish(ctx, camera, parent, "Create Camera");
        }

        template<typename TComponent>
        Entity CreateWith(EditorContext& ctx, const char* name, Entity parent, const char* label) {
            Entity entity = ctx.SceneRef.CreateEntity(name);
            entity.AddComponent<TComponent>();
            return Finish(ctx, entity, parent, label);
        }

        // Unity's GameObject > 3D Object: Mesh Filter (built-in mesh) + Mesh Renderer + the
        // collider Unity gives that primitive, sized to the unit mesh (PrimitiveMeshes.cpp).
        Entity CreatePrimitive(EditorContext& ctx, MeshPrimitive primitive, Entity parent) {
            const char* name = BuiltinMeshes::NameOf(primitive);
            Entity mesh = ctx.SceneRef.CreateEntity(name);
            mesh.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(primitive);
            mesh.AddComponent<MeshRendererComponent>();
            switch (primitive) {
                case MeshPrimitive::Cube:
                    mesh.AddComponent<BoxCollider3DComponent>().Size = { 0.5f, 0.5f, 0.5f }; // half-extents
                    break;
                case MeshPrimitive::Sphere:
                    mesh.AddComponent<SphereCollider3DComponent>().Radius = 0.5f;
                    break;
                case MeshPrimitive::Capsule:
                case MeshPrimitive::Cylinder: { // Unity's Cylinder also gets a Capsule Collider
                    auto& capsule = mesh.AddComponent<CapsuleCollider3DComponent>();
                    capsule.Radius = 0.25f;
                    capsule.Height = 1.0f;
                    break;
                }
                case MeshPrimitive::Plane:
                case MeshPrimitive::Quad:
                default:
                    mesh.AddComponent<MeshColliderComponent>(); // empty Mesh = the Mesh Filter's
                    break;
            }
            return Finish(ctx, mesh, parent, (std::string("Create ") + name).c_str());
        }

        Entity FindCanvasAncestor(Entity entity) {
            while (entity) {
                if (entity.HasComponent<CanvasComponent>())
                    return entity;
                entity = entity.GetComponent<HierarchyComponent>().Parent;
            }
            return Entity{};
        }

        Entity CreateUIElement(EditorContext& ctx, const char* name, Screen screen, Entity parent, bool image, const char* text = nullptr) {
            Entity canvas = FindCanvasAncestor(parent);
            if (!canvas)
                canvas = CreateCanvas(ctx, screen, parent);
            Entity element = ctx.SceneRef.CreateEntity(name);
            element.AddComponent<UIRectComponent>();
            if (image)
                element.AddComponent<UIImageComponent>();
            if (text) {
                auto& textComponent = element.AddComponent<UITextComponent>();
                textComponent.Text = text;
                textComponent.Alignment = TextAlignment::Center;
            }
            // Under the selected UI element when there is one inside the canvas (Unity), else
            // directly under the canvas.
            Entity target = (IsLive(ctx, parent) && FindCanvasAncestor(parent)) ? parent : canvas;
            return Finish(ctx, element, target, (std::string("Create ") + name).c_str());
        }

        void DrawUIScreenMenu(EditorContext& ctx, Screen screen, Entity parent) {
            if (ImGui::MenuItem("Panel")) CreateUIElement(ctx, "Panel", screen, parent, true);
            if (ImGui::MenuItem("Image")) CreateUIElement(ctx, "Image", screen, parent, true);
            if (ImGui::MenuItem("Text")) CreateUIElement(ctx, "Text", screen, parent, false, "Text");
            if (ImGui::MenuItem("Button")) {
                Entity button = CreateUIElement(ctx, "Button", screen, parent, true, "Button");
                button.GetComponent<UIRectComponent>().SizeDelta = { 120.0f, 40.0f };
                button.AddComponent<UIButtonComponent>();
            }
            if (ImGui::MenuItem("Slider")) {
                Entity slider = CreateUIElement(ctx, "Slider", screen, parent, true);
                slider.GetComponent<UIRectComponent>().SizeDelta = { 160.0f, 20.0f };
                slider.AddComponent<UISliderComponent>();
            }
            if (ImGui::MenuItem("Toggle")) {
                Entity toggle = CreateUIElement(ctx, "Toggle", screen, parent, true);
                toggle.GetComponent<UIRectComponent>().SizeDelta = { 24.0f, 24.0f };
                toggle.AddComponent<UIToggleComponent>();
            }
            if (ImGui::MenuItem("Input Field")) {
                Entity field = CreateUIElement(ctx, "Input Field", screen, parent, true);
                field.GetComponent<UIRectComponent>().SizeDelta = { 160.0f, 28.0f };
                field.AddComponent<UIInputFieldComponent>();
            }
            if (ImGui::MenuItem("Dropdown")) {
                Entity dropdown = CreateUIElement(ctx, "Dropdown", screen, parent, true);
                dropdown.GetComponent<UIRectComponent>().SizeDelta = { 160.0f, 26.0f };
                dropdown.AddComponent<UIDropdownComponent>();
            }
            if (ImGui::MenuItem("Scrollbar")) {
                Entity bar = CreateUIElement(ctx, "Scrollbar", screen, parent, true);
                bar.GetComponent<UIRectComponent>().SizeDelta = { 160.0f, 16.0f };
                bar.GetComponent<UIImageComponent>().Color = { 0.25f, 0.25f, 0.25f, 1.0f };
                bar.AddComponent<UIScrollbarComponent>();
            }
            if (ImGui::MenuItem("Scroll View")) {
                // Unity's Scroll View: Scroll Rect + Rect Mask 2D on the view, a Content child
                // taller than the view with a vertical layout, and a vertical Scrollbar.
                Entity view = CreateUIElement(ctx, "Scroll View", screen, parent, true);
                view.GetComponent<UIRectComponent>().SizeDelta = { 200.0f, 140.0f };
                view.GetComponent<UIImageComponent>().Color = { 0.15f, 0.15f, 0.18f, 1.0f };
                view.AddComponent<UIRectMask2DComponent>();
                Entity content = ctx.SceneRef.CreateEntity("Content");
                auto& contentRect = content.AddComponent<UIRectComponent>();
                contentRect.AnchorMin = contentRect.AnchorMax = contentRect.Pivot = { 0.0f, 0.0f };
                contentRect.SizeDelta = { 184.0f, 300.0f };
                content.AddComponent<UILayoutGroupComponent>();
                ctx.SceneRef.SetParent(content, view, Entity{}, false);
                Entity bar = ctx.SceneRef.CreateEntity("Scrollbar Vertical");
                auto& barRect = bar.AddComponent<UIRectComponent>();
                barRect.AnchorMin = { 1.0f, 0.0f };
                barRect.AnchorMax = { 1.0f, 1.0f };
                barRect.Pivot = { 1.0f, 0.5f };
                barRect.SizeDelta = { 12.0f, 0.0f };
                bar.AddComponent<UIImageComponent>().Color = { 0.25f, 0.25f, 0.25f, 1.0f };
                bar.AddComponent<UIScrollbarComponent>().Direction = ScrollbarDirection::BottomToTop;
                ctx.SceneRef.SetParent(bar, view, content, false);
                auto& scroll = view.AddComponent<UIScrollRectComponent>();
                scroll.Content = EntityRef{ static_cast<uint32_t>(content.Handle()) };
                scroll.VerticalScrollbar = EntityRef{ static_cast<uint32_t>(bar.Handle()) };
                scroll.Horizontal = false;
            }
        }

        void Bootstrap3DSScene(EditorContext& ctx) {
            Entity topRoot = ctx.SceneRef.EnsureScreenRoot(Screen::Top);
            Entity bottomRoot = ctx.SceneRef.EnsureScreenRoot(Screen::Bottom);
            for (Screen screen : { Screen::Top, Screen::Bottom }) {
                if (ctx.SceneRef.GetPrimaryCamera(screen))
                    continue;
                Entity camera = ctx.SceneRef.CreateEntity(screen == Screen::Top ? "Top Camera" : "Bottom Camera");
                camera.AddComponent<CameraComponent>().Screen = screen;
                camera.AddComponent<PhysicsRaycaster2DComponent>();
                ctx.SceneRef.SetParent(camera, screen == Screen::Top ? topRoot : bottomRoot);
            }
            SelectOnly(ctx, topRoot);
            MarkSceneDirty(ctx, "Bootstrap 3DS Scene");
        }
    }

    Entity Create3DObject(EditorContext& ctx, const std::string& builtinMeshName, Entity parent) {
        MeshPrimitive primitive;
        if (!BuiltinMeshes::FromName(builtinMeshName, primitive))
            return Entity{};
        return CreatePrimitive(ctx, primitive, IsLive(ctx, parent) ? parent : Entity{});
    }

    void DrawCreateObjectMenu(EditorContext& ctx, Entity parent) {
        if (!IsLive(ctx, parent))
            parent = Entity{};
        if (ImGui::MenuItem(parent ? "Create Empty Child" : "Create Empty", parent ? "Alt+Shift+N" : "Ctrl+Shift+N"))
            CreateEmpty(ctx, parent);
        if (!parent && ImGui::MenuItem("Create Empty Parent", "Ctrl+Shift+G", false, !ctx.SelectedEntities.empty()))
            CreateEmptyParent(ctx);
        ImGui::Separator();
        if (ImGui::BeginMenu("3D Object")) {
            for (const BuiltinMeshes::Info& mesh : BuiltinMeshes::All)
                if (ImGui::MenuItem(mesh.Name))
                    CreatePrimitive(ctx, mesh.Primitive, parent);
            ImGui::Separator();
            if (ImGui::MenuItem("Text - TextMesh")) CreateWith<TextMeshComponent>(ctx, "New Text", parent, "Create Text Mesh");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("2D Object")) {
            if (ImGui::MenuItem("Sprite")) CreateWith<SpriteRendererComponent>(ctx, "Sprite", parent, "Create Sprite");
            if (ImGui::MenuItem("Tilemap")) CreateWith<TilemapComponent>(ctx, "Tilemap", parent, "Create Tilemap");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Effects")) {
            if (ImGui::MenuItem("Particle System")) CreateWith<ParticleSystemComponent>(ctx, "Particle System", parent, "Create Particle System");
            if (ImGui::MenuItem("Line")) CreateWith<LineRendererComponent>(ctx, "Line", parent, "Create Line");
            if (ImGui::MenuItem("Trail")) CreateWith<TrailRendererComponent>(ctx, "Trail", parent, "Create Trail");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Light")) {
            if (ImGui::MenuItem("Directional Light")) {
                Entity light = CreateWith<DirectionalLightComponent>(ctx, "Directional Light", parent, "Create Directional Light");
                light.GetComponent<TransformComponent>().Rotation = { 50.0f, -30.0f, 0.0f };
            }
            if (ImGui::MenuItem("Point Light")) CreateWith<PointLightComponent>(ctx, "Point Light", parent, "Create Point Light");
            if (ImGui::MenuItem("Spot Light")) CreateWith<SpotLightComponent>(ctx, "Spot Light", parent, "Create Spot Light");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Audio")) {
            if (ImGui::MenuItem("Audio Source")) CreateWith<AudioSourceComponent>(ctx, "Audio Source", parent, "Create Audio Source");
            if (ImGui::MenuItem("Audio Listener")) CreateWith<AudioListenerComponent>(ctx, "Audio Listener", parent, "Create Audio Listener");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI")) {
            if (ImGui::BeginMenu("Top Screen")) {
                if (ImGui::MenuItem("Canvas")) CreateCanvas(ctx, Screen::Top, parent);
                ImGui::Separator();
                DrawUIScreenMenu(ctx, Screen::Top, parent);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Bottom Screen")) {
                if (ImGui::MenuItem("Canvas")) CreateCanvas(ctx, Screen::Bottom, parent);
                ImGui::Separator();
                DrawUIScreenMenu(ctx, Screen::Bottom, parent);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            if (ImGui::MenuItem("Top Screen 2D Camera")) CreateCamera(ctx, Screen::Top, ProjectionType::Orthographic, parent);
            if (ImGui::MenuItem("Bottom Screen 2D Camera")) CreateCamera(ctx, Screen::Bottom, ProjectionType::Orthographic, parent);
            ImGui::Separator();
            if (ImGui::MenuItem("Top Screen 3D Camera")) CreateCamera(ctx, Screen::Top, ProjectionType::Perspective, parent);
            if (ImGui::MenuItem("Bottom Screen 3D Camera")) CreateCamera(ctx, Screen::Bottom, ProjectionType::Perspective, parent);
            ImGui::EndMenu();
        }
        if (!parent && ImGui::BeginMenu("3DS Screen Roots")) {
            if (ImGui::MenuItem("Bootstrap 3DS Scene (Roots + Cameras)"))
                Bootstrap3DSScene(ctx);
            if (ImGui::MenuItem("Create Top and Bottom Roots")) {
                ctx.SceneRef.EnsureScreenRoot(Screen::Top);
                ctx.SceneRef.EnsureScreenRoot(Screen::Bottom);
                MarkSceneDirty(ctx, "Create Screen Roots");
            }
            ImGui::EndMenu();
        }
    }

    void DrawEntityEditMenuItems(EditorContext& ctx) {
        const bool hasSelection = !ctx.SelectedEntities.empty();
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSelection))
            CutSelection(ctx);
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
            CopySelection(ctx);
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, HasClipboard(ctx)))
            Paste(ctx, false);
        if (ImGui::MenuItem("Paste As Child", "Ctrl+Shift+V", false, HasClipboard(ctx) && hasSelection))
            Paste(ctx, true);
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2", false, hasSelection))
            ctx.Session.RequestRename = true;
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
            DuplicateSelection(ctx);
        if (ImGui::MenuItem("Delete", "Delete", false, hasSelection))
            DeleteSelection(ctx);
    }

    // ------------------------------------------------------------------------------ Components

    const std::vector<const char*>& ComponentCategories() {
        return ComponentCategory::All();
    }

    // The category is chosen where the component is registered
    // (DualityEngine/Source/Scene/Components/<Domain>Components.cpp).
    const char* ComponentCategoryOf(const std::string& name) {
        const ComponentTypeInfo* type = TypeRegistry::Find(name);
        return type ? type->Category.c_str() : "";
    }

    void AddComponentToSelection(EditorContext& ctx, ComponentTypeInfo& type) {
        bool added = false;
        for (Entity entity : ctx.SelectedEntities) {
            if (!IsLive(ctx, entity) || type.Has(entity))
                continue;
            type.AddDefault(entity);
            added = true;
        }
        if (added)
            MarkSceneDirty(ctx, ("Add " + type.DisplayName).c_str());
    }

    // ------------------------------------------------------------------------------- Play mode

    void StartPlay(EditorContext& ctx) {
        if (ctx.IsPlaying || ctx.IsEditingPrefab)
            return;
        if (!ValidateSceneForRuntime(ctx, "Play"))
            return;
        // An Animation-window preview ends when Play starts: authored values back first, so
        // neither the history nor the Play snapshot contain previewed poses.
        if (ctx.Session.ScenePreviewHook) {
            ctx.Session.ScenePreviewHook(true);
            ctx.Session.ScenePreviewHook = nullptr;
        }
        // Commit any edit still being recorded so it is undoable after Stop.
        if (ctx.Session.ChangeGroupOpen) {
            ctx.History.Capture(ctx.SceneRef, ctx.Session.Ids, ctx.Session.PendingUndoLabel,
                ctx.Session.SelectionBeforeChange, CaptureSelection(ctx));
            ctx.Session.ChangeGroupOpen = false;
            ctx.Session.PendingUndoLabel.clear();
        }
        if (ctx.Session.ClearConsoleOnPlay)
            Log::Clear();
        ctx.PlaySnapshot = SceneSerializer(ctx.SceneRef).SerializeToJson().dump();
        ctx.Session.PlaySnapshotIds = ctx.Session.Ids.CaptureOrder(ctx.SceneRef);
        ctx.Session.PlaySelection = CaptureSelection(ctx);
        ctx.Session.ErrorCountAtLastCheck = Log::GetTotalCount(LogLevel::Error);
        ctx.DraggingGizmoAxis = GizmoAxis::None;
        ctx.IsPlaying = true;
        ctx.SceneRef.OnRuntimeStart();
    }

    void StopPlay(EditorContext& ctx) {
        if (!ctx.IsPlaying)
            return;
        // Unity keeps whatever is selected when Play stops, as long as it existed before Play.
        const HistorySelection selectionAtStop = CaptureSelection(ctx);
        if (ctx.StopAdditiveRuntimeScenes)
            ctx.StopAdditiveRuntimeScenes();
        ctx.SceneRef.OnRuntimeStop();
        ctx.SceneRef.Clear();
        try {
            SceneSerializer(ctx.SceneRef).DeserializeFromJson(nlohmann::json::parse(ctx.PlaySnapshot));
        } catch (const nlohmann::json::exception& error) {
            Log::Error(std::string("Could not restore the scene after Play: ") + error.what());
        }
        ctx.Session.Ids.ApplyOrder(ctx.SceneRef, ctx.Session.PlaySnapshotIds);
        ctx.IsPlaying = false;
        ctx.Session.IsPaused = false;
        ctx.Session.StepRequested = false;
        ctx.DraggingGizmoAxis = GizmoAxis::None;
        ApplySelection(ctx, selectionAtStop);
        if (ctx.SelectedEntities.empty())
            ApplySelection(ctx, ctx.Session.PlaySelection);
    }

    void TogglePlay(EditorContext& ctx) {
        if (ctx.IsPlaying)
            StopPlay(ctx);
        else
            StartPlay(ctx);
    }

    void TogglePause(EditorContext& ctx) {
        ctx.Session.IsPaused = !ctx.Session.IsPaused;
    }

    void Step(EditorContext& ctx) {
        if (!ctx.IsPlaying) {
            ctx.Session.IsPaused = true;
            StartPlay(ctx);
            return;
        }
        ctx.Session.IsPaused = true;
        ctx.Session.StepRequested = true;
    }

    // --------------------------------------------------------------------------------- Keyboard

    void HandleGlobalShortcuts(EditorContext& ctx) {
        ImGuiIO& io = ImGui::GetIO();
        // A focused text field owns every key (including its own Ctrl+Z / Ctrl+C...).
        if (io.WantTextInput)
            return;
        const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
        auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };

        if (ctrl && !alt) {
            // A focused tool window with its own undo stack (Sprite Editor) takes Ctrl+Z / Ctrl+Y.
            const bool windowUndo = ctx.Session.FocusedPanel == EditorPanel::Other && ctx.Session.FocusedWindowUndo;
            if (pressed(ImGuiKey_Z)) {
                if (windowUndo) ctx.Session.FocusedWindowUndo(shift);
                else if (shift) RedoScene(ctx); else UndoScene(ctx);
                return;
            }
            if (pressed(ImGuiKey_Y)) {
                if (windowUndo) ctx.Session.FocusedWindowUndo(true);
                else RedoScene(ctx);
                return;
            }
            // Unity: Ctrl+6 Animation window, Ctrl+7 Profiler.
            if (!shift && pressed(ImGuiKey_6)) {
                AnimationWindow::IsOpen() = true;
                ImGui::SetWindowFocus("Animation");
                return;
            }
            if (!shift && pressed(ImGuiKey_7)) {
                ProfilerWindow::IsOpen() = true;
                ImGui::SetWindowFocus("Profiler");
                return;
            }
            if (pressed(ImGuiKey_S)) {
                if (shift && !ctx.IsEditingPrefab) ctx.RequestSaveSceneAs = true; else SaveScene(ctx);
                return;
            }
            if (pressed(ImGuiKey_N) && !shift) {
                NewSceneWithPrompt(ctx);
                return;
            }
            if (pressed(ImGuiKey_O) && !shift && !ctx.IsEditingPrefab) {
                ctx.RequestOpenSceneDialog = true;
                return;
            }
            if (pressed(ImGuiKey_B) && shift) {
                ctx.ShowBuildSettings = true;
                return;
            }
        }
        // Play mode: Ctrl+P / Ctrl+Shift+P / Ctrl+Alt+P.
        if (ctrl && pressed(ImGuiKey_P)) {
            if (alt) Step(ctx);
            else if (shift) TogglePause(ctx);
            else TogglePlay(ctx);
            return;
        }
        if (ctrl && shift && !alt && pressed(ImGuiKey_N)) {
            CreateEmpty(ctx);
            ctx.Session.RequestRename = ctx.Session.FocusedPanel == EditorPanel::Hierarchy;
            return;
        }
        if (alt && shift && !ctrl && pressed(ImGuiKey_N)) {
            CreateEmpty(ctx, ctx.Selected);
            return;
        }
        if (ctrl && shift && !alt && pressed(ImGuiKey_G)) {
            CreateEmptyParent(ctx);
            return;
        }
        if (alt && shift && !ctrl && pressed(ImGuiKey_A)) {
            ToggleActiveSelection(ctx);
            return;
        }

        // Tool hotkeys (Q/W/E/R/T) -- not while flying with RMB held (WASDQE move the camera).
        if (!ctrl && !alt && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (pressed(ImGuiKey_Q)) ctx.ActiveGizmoMode = GizmoMode::View;
            else if (pressed(ImGuiKey_W)) ctx.ActiveGizmoMode = GizmoMode::Translate;
            else if (pressed(ImGuiKey_E)) ctx.ActiveGizmoMode = GizmoMode::Rotate;
            else if (pressed(ImGuiKey_R)) ctx.ActiveGizmoMode = GizmoMode::Scale;
            else if (pressed(ImGuiKey_T)) ctx.ActiveGizmoMode = GizmoMode::Rect;
            else if (pressed(ImGuiKey_Z) && !shift)
                ctx.Session.ToolHandlePosition = ctx.Session.ToolHandlePosition == HandlePosition::Pivot ? HandlePosition::Center : HandlePosition::Pivot;
            else if (pressed(ImGuiKey_X) && !shift)
                ctx.Session.ToolHandleSpace = ctx.Session.ToolHandleSpace == HandleSpace::Local ? HandleSpace::Global : HandleSpace::Local;
        }

        // Scene-object commands: only while the Hierarchy or the Scene view is the active window.
        const EditorPanel panel = ctx.Session.FocusedPanel;
        if (panel != EditorPanel::Hierarchy && panel != EditorPanel::Scene)
            return;
        if (ctrl && !alt) {
            if (pressed(ImGuiKey_D)) DuplicateSelection(ctx);
            else if (pressed(ImGuiKey_C)) CopySelection(ctx);
            else if (pressed(ImGuiKey_X)) CutSelection(ctx);
            else if (pressed(ImGuiKey_V)) Paste(ctx, shift);
            else if (pressed(ImGuiKey_A)) SelectAll(ctx);
            return;
        }
        if (!ctrl && !alt && !shift) {
            if (pressed(ImGuiKey_Delete))
                DeleteSelection(ctx);
            else if (pressed(ImGuiKey_F2) && ctx.Selected)
                ctx.Session.RequestRename = true;
            else if (pressed(ImGuiKey_F) && ctx.Selected)
                ctx.Session.RequestFrameSelected = true;
        }
        if (shift && !ctrl && pressed(ImGuiKey_Delete))
            DeleteSelection(ctx);
    }

}
