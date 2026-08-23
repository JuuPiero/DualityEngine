#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
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

    // A quad of Size pixels centered on the entity's
    // TransformComponent::Translation. Texture is an AssetRef (empty Guid =
    // no texture assigned) -- when unresolved/absent, renders as a flat
    // Color-filled rect; when it resolves to an image, Color still
    // modulates it (white = unmodified). If the entity also has a
    // SpriteFlipbookComponent, that component's current frame overrides
    // this field entirely while it has a non-empty frame assigned.
    struct SpriteRendererComponent {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec2 Size{ 32.0f, 32.0f };
        AssetRef Texture;
    };

    // Flipbook-style 2D animation: an ordered, fixed-size list of frame
    // textures played back at a constant rate. A fixed 8 slots (not an
    // unbounded list) since the reflection system (Field.h) only models
    // scalar fields, one ImGui widget each -- a real variable-length list
    // editor would need its own, larger extension to TypeRegistry/
    // SceneSerializer/PropertiesPanel. Only advances during Play
    // (Scene::OnRuntimeUpdate), same as Behaviour/physics -- no Edit-mode
    // preview-scrubbing.
    struct SpriteFlipbookComponent {
        AssetRef Frame0, Frame1, Frame2, Frame3, Frame4, Frame5, Frame6, Frame7;
        float FrameDuration = 0.1f;
        bool Loop = true;
        bool Playing = true;

        // Runtime-only, like Rigidbody2DComponent::RuntimeBody -- not
        // reflected/serialized (TypeRegistry only registers the fields
        // above).
        float ElapsedTime = 0.0f;
        int CurrentFrame = 0;
    };

    // Frame0..Frame7 accessed by index -- shared by Scene.cpp (advancing
    // CurrentFrame) and the renderer (picking which frame to draw), so the
    // switch lives in exactly one place.
    inline AssetRef& GetFlipbookFrame(SpriteFlipbookComponent& flipbook, int index) {
        switch (index) {
            case 0: return flipbook.Frame0;
            case 1: return flipbook.Frame1;
            case 2: return flipbook.Frame2;
            case 3: return flipbook.Frame3;
            case 4: return flipbook.Frame4;
            case 5: return flipbook.Frame5;
            case 6: return flipbook.Frame6;
            default: return flipbook.Frame7;
        }
    }

    // Screen is which physical 3DS screen this camera renders to -- the
    // concrete mechanism behind "dual-screen aware from the start". The
    // camera's own TransformComponent::Translation is the world point it's
    // centered on; Zoom is a uniform world-to-pixel scale (1.0 = 1 world
    // unit per pixel, matching every scene authored before Zoom existed).
    // Orthographic drives this screen's existing 2D sprite pipeline (unchanged); Perspective
    // switches that screen over to the 3D mesh pipeline for the frame -- a screen is always
    // fully one or the other, never both composited together (see MeshRendererComponent).
    enum class ProjectionType {
        Orthographic,
        Perspective
    };

    struct CameraComponent {
        Duality::Screen Screen = Duality::Screen::Top;
        bool Primary = true;
        float Zoom = 1.0f; // Orthographic only

        ProjectionType Projection = ProjectionType::Orthographic;
        float FovDegrees = 60.0f; // Perspective only
        float NearPlane = 0.1f;   // Perspective only
        float FarPlane = 1000.0f; // Perspective only
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

    // Unity/Cocos-style parent/child tree. Not registered with TypeRegistry -- like
    // Rigidbody2DComponent::RuntimeBody above, this is engine-managed bookkeeping, not an
    // authored field, so it never appears in the Properties panel and the generic
    // SceneSerializer field loop skips it entirely (SceneSerializer writes/reads the
    // parent relationship itself, as an index, since raw entt::entity handles and Entity
    // pointers don't survive save/load). Parent/Children should only ever be mutated
    // through Scene::SetParent, never assigned directly -- SetParent is what keeps
    // Scene::m_RootEntities and cycle-safety consistent.
    struct HierarchyComponent {
        Entity Parent;                 // null Entity{} = root
        std::vector<Entity> Children;  // order = sibling display/serialization order
    };

    // Opt-in tag marking an entity as a screen's organizational root -- add this to
    // a "TopGroup"/"BottomGroup" entity (or any entity) to associate its whole
    // subtree with a screen for Scene::FindEntityInScreen and the Hierarchy panel's
    // screen-color marker. Entities are never exclusively owned by a screen in this
    // engine (a sprite renders wherever it sits relative to any active camera) --
    // this is a pure organizational/lookup aid, not a hard partition. Reflected
    // (shows up in the Properties panel/Add Component, serialized normally) since,
    // unlike HierarchyComponent, this is meant to be user-authored.
    struct ScreenGroupComponent {
        Duality::Screen Screen = Duality::Screen::Top;
    };

}
