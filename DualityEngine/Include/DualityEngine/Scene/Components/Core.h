#pragma once

// Core components every entity can have: identity (Name, Tag, Active -- Scene/ActiveComponent.h),
// Transform, hierarchy/prefab bookkeeping, render Layer and the Camera. Part of the
// Scene/Components.h umbrella; see that header for the conventions shared by every component.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/ProjectionType.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/ActiveComponent.h"
#include "DualityEngine/Scene/Layer.h"

namespace Duality {

    // The entity's display identifier (Hierarchy label, future Find-by-name)
    // -- kept separate from TagComponent below since they're orthogonal in
    // Unity too (rename an object and its tag/category stays put).
    struct NameComponent {
        std::string Name;
    };

    // A free-text gameplay category, matching Unity's GameObject.tag
    // (Find-by-tag isn't implemented yet, but the field needs to already
    // exist and round-trip through the Inspector/serializer before anything
    // can query by it). "Untagged" mirrors Unity's own default.
    struct TagComponent {
        std::string Tag = "Untagged";
    };

    // ActiveComponent (Unity's GameObject.activeSelf, also mandatory -- added by
    // Scene::CreateEntity right alongside Name/Tag/Transform/Hierarchy) lives in its own
    // Scene/ActiveComponent.h, not here -- see that header's own comment for why.

    // Position/rotation/scale is kept as full 3D vectors even though only 2D
    // rendering exists so far -- the architecture is 2D-first, not 2D-only.
    struct TransformComponent {
        glm::vec3 Translation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };
    };

    struct HierarchyComponent {
        Entity Parent;                 // null Entity{} = root
        std::vector<Entity> Children;  // order = sibling display/serialization order
    };

    // Marks the root of an instantiated prefab.  Only the root carries this component:
    // its descendants are part of the same instance by virtue of the hierarchy.  The
    // AssetRef keeps the connection stable when a prefab is renamed or moved.
    struct PrefabInstanceComponent {
        AssetRef Prefab;
    };

    // Unity-style render layer tag -- replaces the old ScreenGroupComponent. TOP and BOTTOM
    // are the built-in layers mapping to the 3DS's two physical screens; Default means
    // ungrouped (visible wherever a camera actually sees the entity by position). Cameras
    // additionally filter via CameraComponent::CullingMask.
    struct LayerComponent {
        Layer Value = Layer::Default;
    };

    // Screen is which physical 3DS screen this camera renders to -- the
    // concrete mechanism behind "dual-screen aware from the start". The
    // camera's own TransformComponent::Translation is the world point it's
    // centered on; Zoom is a multiplier for the project's Pixels Per Unit
    // (1.0 means one world unit occupies PPU screen pixels).
    // Projection (ProjectionType, Renderer/ProjectionType.h) picks which pipeline this
    // screen uses -- see that header's own comment.
    struct CameraComponent {
        bool Enabled = true;
        Duality::Screen Screen = Duality::Screen::Top;
        bool Primary = true;
        float Zoom = 1.0f; // Orthographic only

        ProjectionType Projection = ProjectionType::Orthographic;
        float FovDegrees = 60.0f; // Perspective only
        float NearPlane = 0.1f;   // Perspective only
        float FarPlane = 1000.0f; // Perspective only

        // The color this camera clears its screen to before drawing -- matches Unity's
        // Camera.backgroundColor. Defaults to the same dark blue-gray RenderScreen's callers
        // (Application.cpp/DualityPlayer's Main.cpp) already hardcoded for the Top screen
        // before this field existed; SceneRenderer.cpp now reads THIS instead whenever a
        // camera is present, falling back to whatever the caller passed only when a screen has
        // no camera at all.
        glm::vec4 Background{ 0.08f, 0.08f, 0.12f, 1.0f };

        // Unity's Camera.cullingMask -- which Layer values this camera draws. Default is every
        // layer (AllLayersMask). Layer::TOP/BOTTOM still gate which physical 3DS screen an
        // entity is associated with; this mask is an additional per-camera filter on top.
        uint32_t CullingMask = AllLayersMask;
    };

}
