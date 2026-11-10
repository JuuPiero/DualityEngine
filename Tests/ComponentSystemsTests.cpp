// Non-physics component systems: serialization (scene JSON, prefab
// copy/paste, undo snapshots), UI layout / mask / Canvas Group / Scroll Rect / Dropdown math,
// constraints, LOD switching, Trail Renderer sampling and Billboard.
#include "TestFramework.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include "DualityEditor/EntityIdRegistry.h"
#include "DualityEditor/SceneHistory.h"
#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Renderer/TrailTextLODRendering.h"
#include "DualityEngine/Renderer/UIWidgetRenderer.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;

namespace {

    const std::set<std::string>& ExtendedTypeNames() {
        static const std::set<std::string> names = {
            "Edge Collider 2D", "Distance Joint 2D", "Hinge Joint 2D", "Spring Joint 2D", "Fixed Joint 2D", "Slider Joint 2D",
            "Wheel Joint 2D", "Friction Joint 2D", "Relative Joint 2D", "Target Joint 2D", "Area Effector 2D", "Point Effector 2D",
            "Surface Effector 2D", "Platform Effector 2D", "Buoyancy Effector 2D", "Constant Force 2D",
            "Fixed Joint", "Hinge Joint", "Spring Joint", "Character Joint", "Mesh Collider", "Character Controller", "Constant Force",
            "UI Scroll Rect", "UI Scrollbar", "UI Mask", "UI Rect Mask 2D", "UI Dropdown", "Canvas Group", "UI Content Size Fitter",
            "UI Aspect Ratio Fitter", "UI Grid Layout Group", "UI Shadow", "UI Outline",
            "Trail Renderer", "Text Mesh", "Billboard", "LOD Group",
            "Position Constraint", "Rotation Constraint", "Scale Constraint", "Parent Constraint", "Look At Constraint", "Aim Constraint",
        };
        return names;
    }

    // A value different from `value` of the same FieldValue alternative.
    FieldValue Mutated(const FieldValue& value, Entity other) {
        return std::visit([&](const auto& v) -> FieldValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int>) return v + 3;
            else if constexpr (std::is_same_v<T, float>) return v + 0.25f;
            else if constexpr (std::is_same_v<T, bool>) return !v;
            else if constexpr (std::is_same_v<T, std::string>) return v + "x";
            else if constexpr (std::is_same_v<T, glm::vec2>) return v + glm::vec2(0.5f, -0.25f);
            else if constexpr (std::is_same_v<T, glm::vec3>) return v + glm::vec3(0.5f, -0.25f, 0.125f);
            else if constexpr (std::is_same_v<T, glm::vec4>) return v + glm::vec4(0.5f);
            else if constexpr (std::is_same_v<T, Color4>) return Color4{ glm::vec4(0.25f, 0.5f, 0.75f, 0.5f) };
            else if constexpr (std::is_same_v<T, AssetRef>) return AssetRef{ "0123456789abcdef0123456789abcdef" };
            else if constexpr (std::is_same_v<T, uint32_t>) return v ^ 0x5u;
            else if constexpr (std::is_same_v<T, EntityRef>) return EntityRef{ static_cast<uint32_t>(other.Handle()) };
            else if constexpr (std::is_same_v<T, std::vector<EntityRef>>) return std::vector<EntityRef>{ EntityRef{ static_cast<uint32_t>(other.Handle()) } };
            else if constexpr (std::is_same_v<T, EnumFieldValue>) {
                EnumFieldValue e = v;
                e.Value = e.Options.empty() ? 0 : (e.Value + 1) % static_cast<int>(e.Options.size());
                return e;
            } else if constexpr (std::is_same_v<T, TextAlignment>) return TextAlignment::Right;
            else return v;
        }, value);
    }

    Entity Named(Scene& scene, const std::string& name) {
        for (auto handle : scene.Registry().view<NameComponent>())
            if (scene.Registry().get<NameComponent>(handle).Name == name)
                return Entity(handle, &scene);
        return {};
    }

    Entity UIRect(Scene& scene, const char* name, Entity parent, glm::vec2 anchoredPosition, glm::vec2 size) {
        Entity e = scene.CreateEntity(name);
        auto& rect = e.AddComponent<UIRectComponent>();
        rect.AnchorMin = rect.AnchorMax = rect.Pivot = { 0.0f, 0.0f };
        rect.AnchoredPosition = anchoredPosition;
        rect.SizeDelta = size;
        if (parent)
            scene.SetParent(e, parent, Entity{}, false);
        return e;
    }

    Entity Canvas(Scene& scene) {
        Entity canvas = scene.CreateEntity("Canvas");
        canvas.AddComponent<CanvasComponent>().Screen = Screen::Top;
        return canvas;
    }

    void Pointer(bool down, glm::vec2 position, Scene& scene) {
        InputManager::BeginFrame();
        InputManager::SetPointer(down, position, Screen::Top);
        UpdateUIInteractions(scene);
    }

    glm::vec3 Forward(const glm::vec3& eulerDegrees) {
        const glm::vec3 r = glm::radians(eulerDegrees);
        const glm::quat q = glm::angleAxis(r.z, glm::vec3(0, 0, 1)) * glm::angleAxis(r.y, glm::vec3(0, 1, 0)) * glm::angleAxis(r.x, glm::vec3(1, 0, 0));
        return q * glm::vec3(0, 0, -1);
    }

    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) { return glm::length(a - b) < eps; }
}

TEST_CASE("Extended components: every field of every new component round-trips through scene JSON") {
    Scene scene;
    Entity other = scene.CreateEntity("Other");
    int registered = 0;
    for (auto& type : TypeRegistry::All()) {
        if (!ExtendedTypeNames().count(type.DisplayName))
            continue;
        ++registered;
        Entity entity = scene.CreateEntity(type.DisplayName);
        type.AddDefault(entity);
        void* component = type.GetPtr(entity);
        for (auto& field : type.Fields)
            field.Set(component, Mutated(field.Get(component), other));
    }
    CHECK(registered == static_cast<int>(ExtendedTypeNames().size()));

    const nlohmann::json first = SceneSerializer(scene).SerializeToJson();
    Scene loaded;
    CHECK(SceneSerializer(loaded).DeserializeFromJson(first));
    const nlohmann::json second = SceneSerializer(loaded).SerializeToJson();
    CHECK_SOFT(first == second, "serialize -> deserialize -> serialize is identical");

    // Spot checks that non-default values really survived (and references were remapped).
    Entity hinge = Named(loaded, "Hinge Joint 2D");
    CHECK(hinge && hinge.HasComponent<HingeJoint2DComponent>());
    CHECK_SOFT(hinge.GetComponent<HingeJoint2DComponent>().UseMotor, "Use Motor flipped and kept");
    CHECK_SOFT(hinge.GetComponent<HingeJoint2DComponent>().ConnectedBody.Handle == static_cast<uint32_t>(Named(loaded, "Other").Handle()),
               "Connected Body remapped to the loaded entity");
    Entity aim = Named(loaded, "Aim Constraint");
    CHECK_SOFT(aim.GetComponent<AimConstraintComponent>().Sources.size() == 1, "entity list kept");
    CHECK_SOFT(aim.GetComponent<AimConstraintComponent>().WorldUpType == ConstraintWorldUpType::ObjectUp, "enum kept by name");
    Entity dropdown = Named(loaded, "UI Dropdown");
    CHECK_SOFT(dropdown.GetComponent<UIDropdownComponent>().Options == "Option A|Option B|Option Cx", "string kept");
}

TEST_CASE("Extended components: old scenes without the new fields load with Unity defaults") {
    // A pre-existing Box Collider 2D entry has no "Used By Effector": must default to false.
    Scene scene;
    Entity e = scene.CreateEntity("Old");
    e.AddComponent<BoxCollider2DComponent>();
    nlohmann::json json = SceneSerializer(scene).SerializeToJson();
    for (auto& entity : json["Entities"])
        if (entity.contains("Box Collider 2D"))
            entity["Box Collider 2D"].erase("Used By Effector");
    Scene loaded;
    CHECK(SceneSerializer(loaded).DeserializeFromJson(json));
    Entity old = Named(loaded, "Old");
    CHECK_SOFT(!old.GetComponent<BoxCollider2DComponent>().UsedByEffector, "missing field -> default false");
    // A partial joint entry keeps the remaining defaults.
    nlohmann::json partial = SceneSerializer(scene).SerializeToJson();
    partial["Entities"][0]["Hinge Joint 2D"] = { { "Use Motor", true } };
    Scene partialScene;
    CHECK(SceneSerializer(partialScene).DeserializeFromJson(partial));
    const auto& hinge = Named(partialScene, "Old").GetComponent<HingeJoint2DComponent>();
    CHECK_SOFT(hinge.UseMotor && hinge.MaxMotorTorque == 10000.0f && hinge.UpperAngle == 359.0f, "unspecified fields keep defaults");
}

TEST_CASE("Extended components: copy/paste remaps joint Connected Body; undo restores edits") {
    Scene scene;
    Entity chain = scene.CreateEntity("Chain");
    Entity link = scene.CreateEntity("Link");
    scene.SetParent(link, chain);
    Entity world = scene.CreateEntity("World");
    auto& hinge = link.AddComponent<HingeJoint2DComponent>();
    hinge.ConnectedBody = EntityRef{ static_cast<uint32_t>(chain.Handle()) };   // internal to the copy
    auto& follow = chain.AddComponent<PositionConstraintComponent>();
    follow.Sources = { EntityRef{ static_cast<uint32_t>(world.Handle()) } };   // external

    const nlohmann::json clipboard = PrefabSerializer::CopyToJson(scene, { chain });
    const std::vector<Entity> pasted = PrefabSerializer::PasteFromJson(scene, clipboard, Entity{});
    CHECK(pasted.size() == 1);
    Entity copy = pasted.front();
    Entity copiedLink = copy.GetComponent<HierarchyComponent>().Children.front();
    CHECK_SOFT(copiedLink.GetComponent<HingeJoint2DComponent>().ConnectedBody.Handle == static_cast<uint32_t>(copy.Handle()),
               "internal Connected Body points at the pasted parent");
    CHECK_SOFT(copy.GetComponent<PositionConstraintComponent>().Sources.front().Handle == static_cast<uint32_t>(world.Handle()),
               "external source kept");

    EntityIdRegistry ids;
    SceneHistory history;
    const uint64_t linkId = ids.Get(link);
    history.Reset(scene, ids, { { linkId }, linkId });
    link.GetComponent<HingeJoint2DComponent>().MotorSpeed = 123.0f;
    CHECK(history.Capture(scene, ids, "Edit Hinge", { { ids.Get(link) }, ids.Get(link) }, { { ids.Get(link) }, ids.Get(link) }));
    CHECK(history.Undo(scene, ids));
    Entity restored = ids.Find(scene, linkId);
    CHECK_SOFT(restored.GetComponent<HingeJoint2DComponent>().MotorSpeed == 0.0f, "undo restored Motor Speed");
    CHECK(history.Redo(scene, ids));
    CHECK_SOFT(ids.Find(scene, linkId).GetComponent<HingeJoint2DComponent>().MotorSpeed == 123.0f, "redo re-applied it");
}

TEST_CASE("UI: Grid Layout Group, Content Size Fitter and Aspect Ratio Fitter") {
    Scene scene;
    Entity canvas = Canvas(scene);
    Entity grid = UIRect(scene, "Grid", canvas, { 0.0f, 0.0f }, { 220.0f, 200.0f });
    auto& layout = grid.AddComponent<UIGridLayoutGroupComponent>();
    layout.CellSize = { 50.0f, 40.0f };
    layout.Spacing = { 10.0f, 10.0f };
    Entity cells[5];
    for (int i = 0; i < 5; ++i)
        cells[i] = UIRect(scene, ("Cell" + std::to_string(i)).c_str(), grid, { 0.0f, 0.0f }, { 1.0f, 1.0f });

    Entity list = UIRect(scene, "List", canvas, { 300.0f, 0.0f }, { 10.0f, 10.0f });
    auto& vertical = list.AddComponent<UILayoutGroupComponent>();
    vertical.Spacing = 4.0f;
    vertical.Padding = { 4.0f, 4.0f };
    UIRect(scene, "A", list, {}, { 60.0f, 20.0f });
    UIRect(scene, "B", list, {}, { 80.0f, 30.0f });
    auto& fitter = list.AddComponent<UIContentSizeFitterComponent>();
    fitter.HorizontalFit = ContentSizeFitMode::PreferredSize;
    fitter.VerticalFit = ContentSizeFitMode::PreferredSize;

    Entity photo = UIRect(scene, "Photo", canvas, { 0.0f, 0.0f }, { 100.0f, 10.0f });
    auto& aspect = photo.AddComponent<UIAspectRatioFitterComponent>();
    aspect.AspectMode = AspectRatioMode::WidthControlsHeight;
    aspect.AspectRatio = 2.0f;

    UpdateUILayoutSystems(scene, 0.0f);
    // Flexible: floor((220 + 10) / 60) = 3 columns.
    CHECK_SOFT(cells[2].GetComponent<UIRectComponent>().AnchoredPosition == glm::vec2(120.0f, 0.0f), "3rd cell ends row 0");
    CHECK_SOFT(cells[3].GetComponent<UIRectComponent>().AnchoredPosition == glm::vec2(0.0f, 50.0f), "4th cell starts row 1");
    CHECK_SOFT(cells[4].GetComponent<UIRectComponent>().SizeDelta == glm::vec2(50.0f, 40.0f), "cells take Cell Size");
    const glm::vec2 fitted = list.GetComponent<UIRectComponent>().SizeDelta;
    CHECK_SOFT(fitted == glm::vec2(88.0f, 62.0f), "fitter = widest child + padding, sum of heights + spacing + padding");
    CHECK_SOFT(photo.GetComponent<UIRectComponent>().SizeDelta.y == 50.0f, "width 100 / ratio 2 = height 50");

    layout.Constraint = GridLayoutConstraint::FixedColumnCount;
    layout.ConstraintCount = 2;
    layout.StartCorner = GridLayoutCorner::UpperRight;
    UpdateUILayoutSystems(scene, 0.0f);
    CHECK_SOFT(cells[0].GetComponent<UIRectComponent>().AnchoredPosition == glm::vec2(60.0f, 0.0f), "Upper Right corner mirrors columns");
    CHECK_SOFT(cells[4].GetComponent<UIRectComponent>().AnchoredPosition == glm::vec2(60.0f, 100.0f), "fixed 2 columns -> 3 rows");
}

TEST_CASE("UI: masks clip (nested intersection), Canvas Group alpha / interactable") {
    Scene scene;
    Entity canvas = Canvas(scene);
    Entity view = UIRect(scene, "View", canvas, { 10.0f, 20.0f }, { 100.0f, 50.0f });
    view.AddComponent<UIMaskComponent>();
    Entity inner = UIRect(scene, "Inner", view, { 50.0f, 0.0f }, { 100.0f, 100.0f });
    auto& rectMask = inner.AddComponent<UIRectMask2DComponent>();
    rectMask.Padding = { 5.0f, 5.0f, 0.0f, 0.0f };
    Entity leaf = UIRect(scene, "Leaf", inner, { 0.0f, 0.0f }, { 10.0f, 10.0f });

    glm::vec4 clip;
    CHECK_SOFT(!ResolveUIClipRect(scene, view, clip), "a mask does not clip its own graphic");
    CHECK(ResolveUIClipRect(scene, inner, clip));
    CHECK_SOFT(clip == glm::vec4(10.0f, 20.0f, 100.0f, 50.0f), "child clipped to the mask rect");
    CHECK(ResolveUIClipRect(scene, leaf, clip));
    // Inner rect = (60, 20)-(160, 120), padded -> (65, 25); intersected with (10,20)-(110,70).
    CHECK_SOFT(clip == glm::vec4(65.0f, 25.0f, 45.0f, 45.0f), "nested masks intersect, padding applied");

    auto& outerGroup = canvas.AddComponent<UICanvasGroupComponent>();
    outerGroup.Alpha = 0.5f;
    auto& innerGroup = view.AddComponent<UICanvasGroupComponent>();
    innerGroup.Alpha = 0.5f;
    CHECK_SOFT(std::fabs(ResolveCanvasGroupAlpha(scene, leaf) - 0.25f) < 1e-6f, "alphas multiply down the hierarchy");
    innerGroup.IgnoreParentGroups = true;
    CHECK_SOFT(std::fabs(ResolveCanvasGroupAlpha(scene, leaf) - 0.5f) < 1e-6f, "Ignore Parent Groups stops the product");

    Entity button = UIRect(scene, "Button", canvas, { 200.0f, 100.0f }, { 40.0f, 20.0f });
    button.AddComponent<UIImageComponent>();
    auto& clickable = button.AddComponent<UIButtonComponent>();
    Pointer(true, { 210.0f, 110.0f }, scene);
    CHECK_SOFT(clickable.IsHovered && clickable.IsPressed, "button reacts normally");
    outerGroup.Interactable = false;
    Pointer(true, { 210.0f, 110.0f }, scene);
    CHECK_SOFT(!clickable.IsHovered, "non-interactable Canvas Group blocks the pointer");
    outerGroup.Interactable = true;
    Pointer(true, { 60.0f, 90.0f }, scene); // inside Leaf's own rect? no -- outside the masks
    CHECK_SOFT(!IsUIPointerAllowed(scene, leaf, { 150.0f, 100.0f }), "pointer outside the mask is ignored");
    CHECK_SOFT(IsUIPointerAllowed(scene, leaf, { 70.0f, 30.0f }), "pointer inside the mask is allowed");
    Pointer(false, { 0.0f, 0.0f }, scene);
}

TEST_CASE("UI: Scroll Rect drag, elastic return, scrollbar sync; Dropdown open/select") {
    Scene scene;
    Entity canvas = Canvas(scene);
    Entity view = UIRect(scene, "View", canvas, { 0.0f, 0.0f }, { 100.0f, 100.0f });
    Entity content = UIRect(scene, "Content", view, { 0.0f, 0.0f }, { 100.0f, 300.0f });
    Entity bar = UIRect(scene, "Bar", canvas, { 120.0f, 0.0f }, { 10.0f, 100.0f });
    auto& scrollbar = bar.AddComponent<UIScrollbarComponent>();
    scrollbar.Direction = ScrollbarDirection::BottomToTop;
    auto& scroll = view.AddComponent<UIScrollRectComponent>();
    scroll.Content = EntityRef{ static_cast<uint32_t>(content.Handle()) };
    scroll.VerticalScrollbar = EntityRef{ static_cast<uint32_t>(bar.Handle()) };
    scroll.Horizontal = false;
    scroll.Inertia = false;

    Pointer(false, { 50.0f, 50.0f }, scene);
    Pointer(true, { 50.0f, 50.0f }, scene);
    Pointer(true, { 50.0f, -50.0f }, scene); // drag up by 100
    auto& contentRect = content.GetComponent<UIRectComponent>();
    CHECK_SOFT(contentRect.AnchoredPosition.y == -100.0f, "content follows the drag");
    Pointer(false, { 50.0f, -50.0f }, scene);
    UpdateUILayoutSystems(scene, 1.0f / 60.0f);
    CHECK_SOFT(std::fabs(scrollbar.Size - 1.0f / 3.0f) < 1e-4f, "scrollbar size = view / content");
    CHECK_SOFT(std::fabs(scrollbar.Value - 0.5f) < 1e-4f, "scrollbar value = scrolled half way (value 1 = top)");
    CHECK_SOFT(std::fabs(scroll.NormalizedPosition.y - 0.5f) < 1e-4f, "normalizedPosition.y = 0.5");

    // Overscroll past the top: elastic spring back to 0.
    Pointer(true, { 50.0f, 0.0f }, scene);
    Pointer(true, { 50.0f, 400.0f }, scene);
    Pointer(false, { 50.0f, 400.0f }, scene);
    CHECK_SOFT(contentRect.AnchoredPosition.y > 0.0f, "rubber-banded past the top while dragging");
    for (int i = 0; i < 120; ++i)
        UpdateUILayoutSystems(scene, 1.0f / 60.0f);
    CHECK_SOFT(std::fabs(contentRect.AnchoredPosition.y) < 0.5f, "Elastic movement returned the content to the top");

    scroll.MovementType = ScrollRectMovementType::Clamped;
    Pointer(true, { 50.0f, 50.0f }, scene);
    Pointer(true, { 50.0f, -1000.0f }, scene);
    Pointer(false, { 50.0f, -1000.0f }, scene);
    CHECK_SOFT(contentRect.AnchoredPosition.y == -200.0f, "Clamped stops at the bottom");

    Entity dropdownEntity = UIRect(scene, "Dropdown", canvas, { 200.0f, 10.0f }, { 120.0f, 20.0f });
    auto& dropdown = dropdownEntity.AddComponent<UIDropdownComponent>();
    dropdown.ItemHeight = 20.0f;
    Pointer(true, { 210.0f, 15.0f }, scene);
    Pointer(false, { 210.0f, 15.0f }, scene);
    CHECK_SOFT(dropdown.IsExpanded, "clicking the caption opens the list");
    Pointer(true, { 210.0f, 55.0f }, scene); // second item: y 50..70
    CHECK_SOFT(!dropdown.IsExpanded && dropdown.Value == 1 && dropdown.WasChanged, "picking an item selects it and closes");
    Pointer(false, { 210.0f, 55.0f }, scene);
    CHECK_SOFT(!dropdown.WasChanged, "WasChanged lasts one frame");
}

TEST_CASE("Constraints: position / rotation / scale / parent / look at / aim") {
    Scene scene;
    Entity source = scene.CreateEntity("Source");
    source.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    source.GetComponent<TransformComponent>().Rotation = { 0.0f, 0.0f, 45.0f };
    source.GetComponent<TransformComponent>().Scale = { 2.0f, 3.0f, 1.0f };
    const EntityRef sourceRef{ static_cast<uint32_t>(source.Handle()) };

    Entity follower = scene.CreateEntity("Follower");
    auto& position = follower.AddComponent<PositionConstraintComponent>();
    position.Sources = { sourceRef };
    position.TranslationOffset = { 0.0f, 1.0f, 0.0f };
    position.ConstrainZ = false;

    Entity spinner = scene.CreateEntity("Spinner");
    spinner.AddComponent<RotationConstraintComponent>().Sources = { sourceRef };
    Entity grower = scene.CreateEntity("Grower");
    auto& scale = grower.AddComponent<ScaleConstraintComponent>();
    scale.Sources = { sourceRef };
    scale.Weight = 0.5f; // halfway between rest (1) and the source

    Entity child = scene.CreateEntity("Child");
    child.GetComponent<TransformComponent>().Translation = { 2.0f, 2.0f, 3.0f }; // 1 unit right of the source
    auto& parent = child.AddComponent<ParentConstraintComponent>();
    parent.Source = sourceRef;

    Entity eye = scene.CreateEntity("Eye");
    eye.AddComponent<LookAtConstraintComponent>().Source = sourceRef;
    Entity turret = scene.CreateEntity("Turret");
    auto& aim = turret.AddComponent<AimConstraintComponent>();
    aim.Sources = { sourceRef };
    aim.AimVector = { 1.0f, 0.0f, 0.0f };
    aim.WorldUpType = ConstraintWorldUpType::None;
    turret.GetComponent<TransformComponent>().Translation = { 1.0f, 0.0f, 3.0f }; // source straight +Y (2 units)

    UpdateLateRuntimeSystems(scene, 0.0f);
    const auto& t = [](Entity e) -> TransformComponent& { return e.GetComponent<TransformComponent>(); };
    CHECK_SOFT(Near(t(follower).Translation, { 1.0f, 3.0f, 0.0f }), "position = source + offset on X/Y; Z unconstrained");
    CHECK_SOFT(Near(t(spinner).Rotation, { 0.0f, 0.0f, 45.0f }, 0.01f), "rotation copied");
    CHECK_SOFT(Near(t(grower).Scale, { 1.5f, 2.0f, 1.0f }), "scale blended with Weight 0.5");
    CHECK_SOFT(Near(t(child).Translation, { 2.0f, 2.0f, 3.0f }), "parent constraint keeps the captured offset");
    CHECK_SOFT(Near(Forward(t(eye).Rotation), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)), 1e-3f), "look at: -Z points at the source");
    CHECK_SOFT(std::fabs(t(turret).Rotation.z - 90.0f) < 0.01f && std::fabs(t(turret).Rotation.x) < 0.01f && std::fabs(t(turret).Rotation.y) < 0.01f,
               "2D aim: pure Z rotation of 90 degrees towards +Y");

    // Move and turn the source: the parent-constrained child orbits with it.
    t(source).Rotation.z = 135.0f;
    UpdateLateRuntimeSystems(scene, 0.0f);
    CHECK_SOFT(Near(t(child).Translation, { 1.0f, 3.0f, 3.0f }, 1e-3f), "child rotated 90 degrees around the source");
    CHECK_SOFT(std::fabs(t(child).Rotation.z - 90.0f) < 0.01f, "and turned with it");
}

TEST_CASE("LOD Group switches by distance, culls, and cleans up when removed") {
    Scene scene;
    Entity group = scene.CreateEntity("Tree");
    Entity high = scene.CreateEntity("High");
    Entity low = scene.CreateEntity("Low");
    Entity lowPart = scene.CreateEntity("LowPart");
    scene.SetParent(high, group);
    scene.SetParent(low, group);
    scene.SetParent(lowPart, low);
    for (Entity e : { high, low, lowPart })
        e.AddComponent<MeshRendererComponent>();
    auto& lod = group.AddComponent<LODGroupComponent>();
    lod.LOD0 = EntityRef{ static_cast<uint32_t>(high.Handle()) };
    lod.LOD1 = EntityRef{ static_cast<uint32_t>(low.Handle()) };
    lod.Distance0 = 10.0f;
    lod.CullDistance = 40.0f;
    const auto hidden = [&](Entity e) { return scene.Registry().all_of<LODHiddenTag>(e.Handle()); };

    ApplyLODGroups(scene, { 0.0f, 0.0f, 5.0f });
    CHECK_SOFT(lod.CurrentLOD == 0 && !hidden(high) && hidden(low) && hidden(lowPart), "close: LOD 0 only (whole LOD 1 subtree hidden)");
    ApplyLODGroups(scene, { 0.0f, 0.0f, 20.0f });
    CHECK_SOFT(lod.CurrentLOD == 1 && hidden(high) && !hidden(low) && !hidden(lowPart), "far: LOD 1 (missing LOD 2/3 fall back)");
    lod.LODBias = 4.0f;
    ApplyLODGroups(scene, { 0.0f, 0.0f, 20.0f });
    CHECK_SOFT(lod.CurrentLOD == 0, "LOD Bias keeps detail longer");
    lod.LODBias = 1.0f;
    ApplyLODGroups(scene, { 0.0f, 0.0f, 50.0f });
    CHECK_SOFT(lod.CurrentLOD == -1 && hidden(high) && hidden(low), "beyond Cull Distance everything is hidden");
    group.RemoveComponent<LODGroupComponent>();
    ApplyLODGroups(scene, { 0.0f, 0.0f, 50.0f });
    CHECK_SOFT(!hidden(high) && !hidden(low) && !hidden(lowPart), "removing the LOD Group shows everything again");
}

TEST_CASE("Trail Renderer: samples by Min Vertex Distance, bounded ring, expires after Time; Billboard aligns") {
    Scene scene;
    Entity comet = scene.CreateEntity("Comet");
    auto& trail = comet.AddComponent<TrailRendererComponent>();
    trail.Time = 0.5f;
    trail.MinVertexDistance = 0.09f;
    auto& position = comet.GetComponent<TransformComponent>().Translation;
    for (int i = 0; i < 10; ++i) {
        position.x = i * 0.05f; // only every 2nd step is far enough
        UpdateLateRuntimeSystems(scene, 0.01f);
    }
    CHECK_SOFT(trail.RuntimeCount == 5, "points every Min Vertex Distance");
    for (int i = 0; i < 200; ++i) {
        position.x += 1.0f;
        UpdateLateRuntimeSystems(scene, 0.001f);
    }
    CHECK_SOFT(trail.RuntimeCount == MaxTrailPoints, "ring buffer bounded to MaxTrailPoints");
    for (int i = 0; i < 60; ++i)
        UpdateLateRuntimeSystems(scene, 0.02f); // standing still for 1.2 s > Time
    CHECK_SOFT(trail.RuntimeCount == 1, "old points expire after Time (one fresh point at the head)");
    trail.Emitting = false;
    UpdateLateRuntimeSystems(scene, 1.0f);
    CHECK_SOFT(trail.RuntimeCount == 0, "not emitting: empties");

    Entity camera = scene.CreateEntity("Camera");
    auto& cameraComponent = camera.AddComponent<CameraComponent>();
    cameraComponent.Projection = ProjectionType::Perspective;
    camera.GetComponent<TransformComponent>().Rotation = { 10.0f, 20.0f, 0.0f };
    Entity sign = scene.CreateEntity("Sign");
    sign.AddComponent<BillboardComponent>();
    UpdateLateRuntimeSystems(scene, 0.0f);
    CHECK_SOFT(Near(sign.GetComponent<TransformComponent>().Rotation, { 10.0f, 20.0f, 0.0f }, 0.01f), "Align With Camera copies the camera rotation");
}
