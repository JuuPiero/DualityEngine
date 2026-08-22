#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Behaviour.h"

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

    // Position/rotation/scale is kept as full 3D vectors even though only 2D
    // rendering exists so far -- the architecture is 2D-first, not 2D-only.
    struct TransformComponent {
        glm::vec3 Translation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };
    };

    // No texture support yet (Phase 0) -- a flat-colored quad of Size pixels
    // centered on the entity's TransformComponent::Translation.
    struct SpriteRendererComponent {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec2 Size{ 32.0f, 32.0f };
    };

    // Screen is which physical 3DS screen this camera renders to -- the
    // concrete mechanism behind "dual-screen aware from the start". The
    // camera's own TransformComponent::Translation is the world point it's
    // centered on; Zoom is a uniform world-to-pixel scale (1.0 = 1 world
    // unit per pixel, matching every scene authored before Zoom existed).
    struct CameraComponent {
        Duality::Screen Screen = Duality::Screen::Top;
        bool Primary = true;
        float Zoom = 1.0f;
    };

    // Which Behaviour subclass is attached, looked up by name at Play time
    // via ScriptRegistry -- not a compile-time binding, so the Inspector can
    // show/edit it as plain text (like Unity picking a MonoBehaviour by
    // class) and so a hot-reloaded script DLL can supply a new instance
    // without the entity needing to change.
    struct BehaviourComponent {
        std::string ClassName;
        Behaviour* Instance = nullptr;
        void (*Destroy)(Behaviour*) = nullptr;
    };

    // Box2D-backed 2D physics (classic v2.4 API, b2World owned by Scene).
    // RuntimeBody/RuntimeFixture are opaque (void*, actually b2Body*/
    // b2Fixture*) so this header doesn't need to include Box2D itself --
    // only valid between Scene::OnRuntimeStart and OnRuntimeStop, never
    // serialized (TypeRegistry only ever registers the authored fields
    // below, per-field opt-in).
    struct Rigidbody2DComponent {
        bool IsStatic = false;
        bool FixedRotation = false;
        void* RuntimeBody = nullptr;
    };

    struct BoxCollider2DComponent {
        glm::vec2 Offset{ 0.0f, 0.0f };
        glm::vec2 Size{ 16.0f, 16.0f }; // half-extents, in the same world units as Transform
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        void* RuntimeFixture = nullptr;
    };

    struct CircleCollider2DComponent {
        glm::vec2 Offset{ 0.0f, 0.0f };
        float Radius = 16.0f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        void* RuntimeFixture = nullptr;
    };

}
