#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/MeshPrimitive.h"
#include "DualityEngine/Renderer/ProjectionType.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Renderer/UIAnchor.h"
#include "DualityEngine/Scene/ActiveComponent.h"
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

    // Unlit -- no lighting/material system yet (see ROADMAP.md). A procedural primitive
    // (no mesh import pipeline yet) sized/oriented by the entity's own TransformComponent
    // (world-space Scale IS the mesh's size here, unlike SpriteRendererComponent's separate
    // Size field, since a 3D mesh has no meaningful "pixel size" the way a 2D quad does).
    // Color/Texture follow SpriteRendererComponent's exact convention (empty Guid = flat
    // Color, Color still modulates a resolved texture). Only drawn on a screen whose primary
    // camera is ProjectionType::Perspective -- see CameraComponent. MeshPrimitive itself lives
    // in Renderer/MeshPrimitive.h, not here -- see that header's own comment for why.
    struct MeshRendererComponent {
        // Used only when Mesh (below) is empty/unresolved.
        MeshPrimitive Primitive = MeshPrimitive::Cube;
        // One ".mat" asset guid (see Asset/Material.h) per submesh, resolved via MaterialLoader
        // at render time -- index-matched against Mesh's own material-group ranges (MeshLoader.h's
        // MeshData::SubMesh, split on the imported ".obj"'s own "usemtl" boundaries), same
        // slot convention as Unity's Renderer.materials. Fewer entries than the mesh has
        // submeshes repeats the LAST entry for the remaining ones; an empty list (or an
        // unresolved/empty guid in a slot) falls back to a default white material, matching
        // every other AssetRef's own graceful-degradation convention. A procedural Primitive
        // (Mesh empty/unresolved) has no submesh concept -- only index 0 is ever used.
        std::vector<AssetRef> Materials;
        // Guid of an imported ".obj" mesh (see Asset/MeshLoader.h) -- empty/unresolved falls
        // back to the procedural Primitive above, same convention as Materials/Texture.
        AssetRef Mesh;
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
    // Projection (ProjectionType, Renderer/ProjectionType.h) picks which pipeline this
    // screen uses -- see that header's own comment.
    struct CameraComponent {
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
    };

    // One attached script -- which Behaviour subclass, looked up by name at Play time via
    // ScriptRegistry (not a compile-time binding, so a hot-reloaded script DLL can supply a new
    // instance without the entity needing to change). An entity can hold many of these at once
    // (see BehaviourComponent::Scripts below), matching Unity letting one GameObject carry many
    // different MonoBehaviours.
    struct ScriptInstance {
        std::string ClassName;
        Behaviour* Instance = nullptr;
        void (*Destroy)(Behaviour*) = nullptr;

        // Edit-mode values for whichever DUALITY_PROPERTY fields ClassName's script declares
        // (see Reflection/PropertyMacros.h/ScriptRegistry::GetFields) -- keyed by field name.
        // Not reflected via TypeRegistry (a map isn't a plain FieldValue), so
        // EntitySerialization.cpp special-cases it alongside "Class", same as
        // HierarchyComponent's Parent. Applied onto the real Instance right before OnCreate()
        // at Play start (Scene::OnRuntimeStart); while Instance is alive, the Properties panel
        // edits it directly instead and this map is left stale until OnRuntimeStop.
        std::unordered_map<std::string, FieldValue> PropertyOverrides;

        // Runtime-only (like RuntimeBody below), not reflected -- lets Scene::OnRuntimeUpdate
        // edge-detect an ActiveComponent transition to fire OnEnable/OnDisable exactly once per
        // flip instead of every frame while active/inactive. Starts false (not matching
        // ActiveComponent's own true default) so the very first OnRuntimeUpdate tick after
        // OnCreate() correctly sees "became active" as a transition and fires one OnEnable,
        // for an entity that starts active -- matching Unity's Awake-then-OnEnable ordering.
        // An entity that starts inactive never transitions away from false, so it correctly
        // never gets an OnEnable/OnDisable pair at all until something actually activates it.
        bool WasActiveLastFrame = false;
    };

    // Holds every script attached to one entity. Deliberately still ONE EnTT component type
    // (not one type per script class -- EnTT has no first-class support for runtime-registered
    // component types, and this engine's reflection is built around compile-time
    // pointer-to-member anyway) wrapping a vector of slots instead, so an entity can run several
    // different scripts (or the same one more than once, like Unity allows) simultaneously. The
    // Properties panel renders each slot as its own visually distinct card.
    struct BehaviourComponent {
        std::vector<ScriptInstance> Scripts;
    };

    // Box2D-backed 2D physics (classic v2.4 API, b2World owned by Scene).
    // RuntimeBody/RuntimeFixture are opaque (void*, actually b2Body*/
    // b2Fixture*) so this header doesn't need to include Box2D itself --
    // only valid between Scene::OnRuntimeStart and OnRuntimeStop, never
    // serialized (TypeRegistry only ever registers the authored fields
    // below, per-field opt-in).
    struct Rigidbody2DComponent {
        BodyType Type = BodyType::Dynamic;
        bool FixedRotation = false;
        void* RuntimeBody = nullptr;
    };

    struct BoxCollider2DComponent {
        glm::vec2 Offset{ 0.0f, 0.0f };
        glm::vec2 Size{ 16.0f, 16.0f }; // half-extents, in the same world units as Transform
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        // Box2D's own native sensor flag (fixtureDef.isSensor) -- a trigger still generates
        // Begin/EndContact (so OnTriggerEnter/Exit fire) but never physically resolves the
        // overlap. Unity's own rule: a contact fires as a TRIGGER callback if EITHER side is
        // a trigger, and as a COLLISION callback only when NEITHER side is.
        bool IsTrigger = false;
        // When on, the Scene view shows draggable resize handles on this collider's outline
        // (see ScenePanel.cpp's DragCollider2DHandle) -- off by default so moving/inspecting an
        // entity in the viewport never risks an accidental collider reshape from a stray drag.
        // The green wireframe outline itself is shown whenever the entity is selected,
        // regardless of this flag; this only gates whether the handles are draggable.
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

    struct CircleCollider2DComponent {
        glm::vec2 Offset{ 0.0f, 0.0f };
        float Radius = 16.0f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false; // see BoxCollider2DComponent::IsTrigger
        bool EditMode = false; // see BoxCollider2DComponent::EditMode
        void* RuntimeFixture = nullptr;
    };

    // Bullet-backed 3D physics (btDiscreteDynamicsWorld owned by Scene, see
    // Physics3DWorld in Scene.cpp), same shape/lifetime convention as the
    // Box2D components above: RuntimeBody is opaque (actually btRigidBody*)
    // so this header doesn't need to include Bullet, only valid between
    // Scene::OnRuntimeStart and OnRuntimeStop, never serialized.
    //
    // Unlike Box2D (whose b2World owns and frees every fixture it creates),
    // Bullet does not take ownership of a body's btCollisionShape -- the
    // caller must free it itself. RuntimeCollisionShape tracks exactly that
    // one shape (the box/sphere itself, or -- when a collider's Offset is
    // non-zero -- the btCompoundShape wrapping it) so Scene::OnRuntimeStop
    // can delete it regardless of which collider component (if any) is
    // present; the collider components below don't need their own runtime
    // pointer at all as a result.
    struct Rigidbody3DComponent {
        BodyType Type = BodyType::Dynamic;
        void* RuntimeBody = nullptr;
        void* RuntimeCollisionShape = nullptr;
    };

    struct BoxCollider3DComponent {
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Size{ 16.0f, 16.0f, 16.0f }; // half-extents, in the same world units as Transform
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        // Bullet has no native per-shape sensor concept the way Box2D does -- a trigger body
        // gets btCollisionObject::CF_NO_CONTACT_RESPONSE set at creation instead (contact
        // manifolds still generate, so the manual enter/exit diffing in Scene.cpp still
        // detects it, but the physical push-apart response is suppressed). Same
        // "either side is a trigger -> trigger callback" rule as the 2D colliders.
        bool IsTrigger = false;
        bool EditMode = false; // see BoxCollider2DComponent::EditMode
    };

    struct SphereCollider3DComponent {
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        float Radius = 16.0f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false; // see BoxCollider3DComponent::IsTrigger
        bool EditMode = false; // see BoxCollider2DComponent::EditMode
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

    // --- UI (screen-space overlay, drawn last/on top every frame -- see Renderer/UIRenderer.h)
    // ---
    //
    // Deliberately separate from TransformComponent/world-space rendering: a UI element lives
    // in a fixed physical screen's own pixel space, with no camera involved at all (no pan/zoom
    // relative to gameplay), matching Unity's Canvas "Screen Space - Overlay" render mode --
    // the only mode this engine supports (no "Screen Space - Camera"/"World Space" canvas modes
    // yet). Every UI widget type follows the same two-component split as this first one
    // (UIRectComponent for where/how big, a second component for what it looks like/does) --
    // adding a new widget type later (e.g. a slider) means adding one new component + one loop
    // in UIRenderer.cpp, without touching this positioning component at all.

    // Where/how big a UI element is: Anchor + Offset (pixels from that anchor corner/edge) +
    // Size (pixels) resolves to a rect in `Screen`'s own fixed pixel space (see
    // Renderer/UIRenderer.h's ResolveUIRect) -- entirely independent of any CameraComponent.
    struct UIRectComponent {
        Duality::Screen Screen = Duality::Screen::Top;
        UIAnchor Anchor = UIAnchor::TopLeft;
        glm::vec2 Offset{ 0.0f, 0.0f };
        glm::vec2 Size{ 100.0f, 40.0f };
    };

    // The visual half of a basic Panel/Image widget (pair with UIRectComponent). Same
    // Color/Texture convention as SpriteRendererComponent (empty Texture Guid = flat Color).
    // If the same entity also has a UIButtonComponent, that component's own
    // Normal/Hover/Pressed color takes over instead of this Color -- see UIButtonComponent.
    struct UIImageComponent {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        AssetRef Texture;
    };

    // Makes a (UIRectComponent, UIImageComponent) pair clickable -- pair all three on one
    // entity for a basic Button. IsHovered/IsPressed/WasClicked are updated once per frame by
    // UIRenderer.cpp's UpdateUIInteractions (called once per frame by the app entry point,
    // before Scene::OnRuntimeUpdate so scripts see this frame's state) and are meant to be
    // polled from a script the same way GetKeyDown() is: `GetComponent<UIButtonComponent>().
    // WasClicked`. Not reflected as authored-editable at Play time (IsHovered/IsPressed/
    // WasClicked are runtime state, like SpriteFlipbookComponent::CurrentFrame) -- only the
    // three colors are.
    struct UIButtonComponent {
        glm::vec4 NormalColor{ 0.85f, 0.85f, 0.85f, 1.0f };
        glm::vec4 HoverColor{ 0.95f, 0.95f, 0.6f, 1.0f };
        glm::vec4 PressedColor{ 0.7f, 0.7f, 0.4f, 1.0f };

        bool IsHovered = false;
        bool IsPressed = false;
        bool WasClicked = false; // true for exactly one frame: pointer released while still over the button
    };

    // A text label's string content (pair with UIRectComponent for a <Text> widget, see
    // UIDocument.h). Data-only for now, deliberately -- there is no font/glyph rendering system
    // on either backend yet (citro2d has one built in on 3DS; desktop's OpenGLRenderer2D is
    // legacy fixed-function with no font atlas at all), so UIRenderer.cpp does not draw this
    // Text anywhere yet. Exists now so UIDocument markup can already author/round-trip text
    // content ahead of that renderer work, rather than the two being coupled into one big change.
    struct UITextComponent {
        std::string Text;
    };

}
