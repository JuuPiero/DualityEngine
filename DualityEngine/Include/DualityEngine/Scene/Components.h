#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Physics/BodyType.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/CanvasRenderMode.h"
#include "DualityEngine/Renderer/MeshPrimitive.h"
#include "DualityEngine/Renderer/ProjectionType.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Renderer/TextAlignment.h"
#include "DualityEngine/Renderer/UIAnchor.h"
#include "DualityEngine/Renderer/UILayoutType.h"
#include "DualityEngine/Scene/ActiveComponent.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Audio/AudioEngine.h"

namespace Duality {

    class Behaviour;

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
        bool Enabled = true;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec2 Size{ 32.0f, 32.0f };
        AssetRef Texture;
        int SortOrder = 0;
        bool FlipX = false;
        bool FlipY = false;
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec4 SliceBorder{ 0.0f, 0.0f, 0.0f, 0.0f }; // left, right, top, bottom px for 9-slice (0 = disabled)
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
        bool Enabled = true;
        // Used only when Mesh (below) is empty/unresolved.
        MeshPrimitive Primitive = MeshPrimitive::Cube;
        // One ".mat" asset guid (see Asset/Material.h) per submesh, resolved via MaterialLoader
        // at render time -- index-matched against Mesh's own part ranges (MeshLoader.h's
        // MeshData::SubMesh, split on the imported ".obj"'s "usemtl" / "o" / "g" boundaries).
        // Slot i paints submesh i only. A single entry still covers the whole mesh (one look).
        // Two or more entries do not repeat the last slot onto leftover parts -- those stay
        // the default white material. An empty list (or an unresolved/empty guid in a slot)
        // falls back to that same default. A procedural Primitive (Mesh empty/unresolved)
        // has no submesh concept -- only index 0 is ever used.
        std::vector<AssetRef> Materials;
        // Guid of an imported ".obj" mesh (see Asset/MeshLoader.h) -- empty/unresolved falls
        // back to the procedural Primitive above, same convention as Materials/Texture.
        AssetRef Mesh;
        int SortOrder = 0;
    };

    // Slot i paints submesh i. One assigned material still covers every part; extra parts
    // beyond the list stay default instead of inheriting the last slot (that repeat made
    // material 2, then 3, look like they overpainted the whole mesh).
    inline const AssetRef& MaterialForSubMesh(const std::vector<AssetRef>& materials, uint32_t subMeshIndex) {
        static const AssetRef none{};
        if (materials.empty())
            return none;
        if (materials.size() == 1)
            return materials[0];
        if (subMeshIndex < materials.size())
            return materials[subMeshIndex];
        return none;
    }

    // Flipbook-style 2D animation: an ordered, fixed-size list of frame
    // textures played back at a constant rate. A fixed 8 slots (not an
    // unbounded list) since the reflection system (Field.h) only models
    // scalar fields, one ImGui widget each -- a real variable-length list
    // editor would need its own, larger extension to TypeRegistry/
    // SceneSerializer/PropertiesPanel. Only advances during Play
    // (Scene::OnRuntimeUpdate), same as Behaviour/physics -- no Edit-mode
    // preview-scrubbing.
    struct SpriteFlipbookComponent {
        bool Enabled = true;
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

    // Unity Physics2DRaycaster equivalent -- pair with CameraComponent on the same entity.
    // UpdatePhysicsRaycasterInteractions reads the pointer each frame, point-queries Box2D
    // colliders through this camera's orthographic screen mapping, and dispatches IPointer*
    // callbacks on hit entities' scripts. Auto-added with Camera when Projection is
    // Orthographic (see Reflection.cpp).
    struct PhysicsRaycaster2DComponent {
        bool Enabled = true;

        // Runtime-only (not reflected/serialized).
        entt::entity HoveredEntity = entt::null;
        entt::entity PressedEntity = entt::null;
        Screen PressedScreen = Screen::Top;
        glm::vec2 PressedPosition{ 0.0f };
    };

    // Unity PhysicsRaycaster equivalent -- pair with CameraComponent on the same entity.
    // UpdatePhysicsRaycasterInteractions casts a 3D ray from this camera through the pointer
    // and dispatches IPointer* callbacks on Bullet collider hits. Auto-added with Camera when
    // Projection is Perspective (see Reflection.cpp).
    struct PhysicsRaycaster3DComponent {
        bool Enabled = true;
        float MaxDistance = 2000.0f;

        // Runtime-only (not reflected/serialized).
        entt::entity HoveredEntity = entt::null;
        entt::entity PressedEntity = entt::null;
        Screen PressedScreen = Screen::Top;
        glm::vec2 PressedPosition{ 0.0f };
    };

    // One attached script -- which Behaviour subclass, looked up by name at Play time via
    // ScriptRegistry (not a compile-time binding, so a hot-reloaded script DLL can supply a new
    // instance without the entity needing to change). An entity can hold many of these at once
    // (see BehaviourComponent::Scripts below), matching Unity letting one GameObject carry many
    // different MonoBehaviours.
    struct ScriptInstance {
        std::string ClassName;
        // Unity's MonoBehaviour.enabled -- authored/serialized per script slot. OnUpdate and
        // collision/pointer callbacks only run when this is true AND the entity is
        // IsEffectivelyActive; flipping either edge fires OnEnable/OnDisable (see
        // WasEnabledLastFrame). Independent of ActiveComponent (entity on/off).
        bool Enabled = true;

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

        // Runtime-only (like RuntimeBody below), not reflected -- edge-detects
        // (IsEffectivelyActive && Enabled) so OnEnable/OnDisable fire once per flip.
        // Starts false so the first OnRuntimeUpdate after OnCreate sees "became enabled"
        // as a transition (Unity Awake-then-OnEnable). An entity/script that starts
        // inactive/disabled never transitions away from false until something enables it.
        bool WasEnabledLastFrame = false;
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
        bool Enabled = true;
        BodyType Type = BodyType::Dynamic;
        bool FixedRotation = false;
        float Mass = 0.0f; // 0 = derive from collider density
        float LinearDrag = 0.0f;
        float AngularDrag = 0.05f;
        float GravityScale = 1.0f;
        bool UseGravity = true;
        void* RuntimeBody = nullptr;
    };

    struct BoxCollider2DComponent {
        bool Enabled = true;
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
        AssetRef PhysicsMaterial;
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

    struct CircleCollider2DComponent {
        bool Enabled = true;
        glm::vec2 Offset{ 0.0f, 0.0f };
        float Radius = 16.0f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false; // see BoxCollider2DComponent::IsTrigger
        AssetRef PhysicsMaterial;
        bool EditMode = false; // see BoxCollider2DComponent::EditMode
        void* RuntimeFixture = nullptr;
    };

    // Capsule (2D) -- approximated as a fixed 8-vertex polygon for Box2D v2.4.
    struct CapsuleCollider2DComponent {
        bool Enabled = true;
        glm::vec2 Offset{ 0.0f, 0.0f };
        float Radius = 8.0f;
        float Height = 32.0f; // total height including hemispheres
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

    // Convex polygon (max 8 vertices) for Box2D.
    struct PolygonCollider2DComponent {
        bool Enabled = true;
        glm::vec2 Offset{ 0.0f, 0.0f };
        int VertexCount = 4;
        glm::vec2 Vertex0{ -8.0f, -8.0f };
        glm::vec2 Vertex1{ 8.0f, -8.0f };
        glm::vec2 Vertex2{ 8.0f, 8.0f };
        glm::vec2 Vertex3{ -8.0f, 8.0f };
        glm::vec2 Vertex4{}, Vertex5{}, Vertex6{}, Vertex7{};
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

    // Bullet-backed 3D physics (btDiscreteDynamicsWorld owned by Scene, see
    // Physics3DWorld in Scene.cpp), same shape/lifetime convention as the
    // Box2D components above: RuntimeBody is opaque (actually btRigidBody*)
    // so this header doesn't need to include Bullet, only valid between
    // Scene::OnRuntimeStart and OnRuntimeStop, never serialized.
    struct Rigidbody3DComponent {
        bool Enabled = true;
        BodyType Type = BodyType::Dynamic;
        float Mass = 0.0f;
        float LinearDrag = 0.0f;
        float AngularDrag = 0.05f;
        float GravityScale = 1.0f;
        bool UseGravity = true;
        void* RuntimeBody = nullptr;
        void* RuntimeCollisionShape = nullptr;
    };

    struct BoxCollider3DComponent {
        bool Enabled = true;
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
        AssetRef PhysicsMaterial;
        bool EditMode = false;
    };

    struct SphereCollider3DComponent {
        bool Enabled = true;
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        float Radius = 16.0f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
    };

    struct CapsuleCollider3DComponent {
        bool Enabled = true;
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        float Radius = 8.0f;
        float Height = 32.0f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
    };

    struct HierarchyComponent {
        Entity Parent;                 // null Entity{} = root
        std::vector<Entity> Children;  // order = sibling display/serialization order
    };

    // Unity-style render layer tag -- replaces the old ScreenGroupComponent. TOP and BOTTOM
    // are the built-in layers mapping to the 3DS's two physical screens; Default means
    // ungrouped (visible wherever a camera actually sees the entity by position). Cameras
    // additionally filter via CameraComponent::CullingMask.
    struct LayerComponent {
        Layer Value = Layer::Default;
    };

    // Unity's AudioSource -- per-entity clip playback with volume/loop/pause control.
    // RuntimeHandle is engine-managed (see AudioEngine.h), not serialized.
    struct AudioSourceComponent {
        bool Enabled = true;
        AssetRef Clip;
        bool Loop = false;
        float Volume = 1.0f;
        bool PlayOnAwake = false;
        bool Mute = false;
        float SpatialBlend = 0.0f;
        float MinDistance = 1.0f;
        float MaxDistance = 500.0f;

        AudioHandle RuntimeHandle = InvalidAudioHandle;
        bool Paused = false;
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

    // Where/how big a UI element is -- Unity RectTransform-equivalent. AnchorMin/AnchorMax are
    // normalized [0,1] positions within the parent rect (or the physical screen, at the root);
    // equal on an axis means a point anchor, different means that axis stretches with the
    // parent. Pivot is a normalized [0,1] point within this rect's own resolved bounds.
    // AnchoredPosition offsets the pivot from the anchor point (point-anchor axes) or shifts a
    // stretched rect from the parent's own center (stretch axes; ignored on that axis
    // otherwise -- see Renderer/UIRenderer.h's ResolveUIRect). SizeDelta is this rect's size on
    // a point-anchor axis, or the amount added to the anchor-implied size on a stretch axis.
    // UIAnchor (Renderer/UIAnchor.h) is kept only as an editor-side "anchor preset" convenience
    // for quick-setting AnchorMin/AnchorMax/Pivot -- it is not stored on the component itself.
    struct UIRectComponent {
        bool Enabled = true;
        Duality::Screen Screen = Duality::Screen::Top;
        glm::vec2 AnchorMin{ 0.5f, 0.5f };
        glm::vec2 AnchorMax{ 0.5f, 0.5f };
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec2 AnchoredPosition{ 0.0f, 0.0f };
        glm::vec2 SizeDelta{ 100.0f, 40.0f };
        int SortOrder = 0;
    };

    // The visual half of a basic Panel/Image widget (pair with UIRectComponent). Same
    // Color/Texture convention as SpriteRendererComponent (empty Texture Guid = flat Color).
    // If the same entity also has a UIButtonComponent, that component's own
    // Normal/Hover/Pressed color takes over instead of this Color -- see UIButtonComponent.
    struct UIImageComponent {
        bool Enabled = true;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        AssetRef Texture;
        glm::vec4 SliceBorder{ 0.0f, 0.0f, 0.0f, 0.0f };
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
        bool Enabled = true;
        glm::vec4 NormalColor{ 0.85f, 0.85f, 0.85f, 1.0f };
        glm::vec4 HoverColor{ 0.95f, 0.95f, 0.6f, 1.0f };
        glm::vec4 PressedColor{ 0.7f, 0.7f, 0.4f, 1.0f };

        bool IsHovered = false;
        bool IsPressed = false;
        bool WasClicked = false; // true for exactly one frame: pointer released while still over the button
    };

    // A text label (pair with UIRectComponent for a <Text> widget, see UIDocument.h). Drawn by
    // UIRenderer.cpp via IRenderer2D::DrawText, horizontally positioned within the paired
    // UIRectComponent's resolved rect per Alignment -- vertical is always centered (a
    // deliberate v1 scope limit, no separate vertical-alignment enum). Font is an AssetRef to a
    // .ttf/.otf (empty = default/system font); real per-platform behavior differs -- see
    // IRenderer2D::LoadFont's own comment (desktop rasterizes any assigned font for real,
    // 3DS always uses citro2d's built-in system font regardless, logged once if ignored).
    struct UITextComponent {
        bool Enabled = true;
        std::string Text;
        AssetRef Font;
        float FontSize = 16.0f;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        TextAlignment Alignment = TextAlignment::Left;
    };

    // Unity Canvas root -- children UI widgets inherit render mode / sort.
    struct CanvasComponent {
        bool Enabled = true;
        CanvasRenderMode RenderMode = CanvasRenderMode::ScreenSpaceOverlay;
        int SortOrder = 0;
        float ScaleFactor = 1.0f;
        EntityRef TargetCamera; // for ScreenSpaceCamera
    };

    struct UILayoutGroupComponent {
        bool Enabled = true;
        UILayoutType Layout = UILayoutType::Vertical;
        float Spacing = 4.0f;
        glm::vec2 Padding{ 4.0f, 4.0f };
        bool ChildControlWidth = true;
        bool ChildControlHeight = true;
    };

    struct UISliderComponent {
        bool Enabled = true;
        float Value = 0.5f;
        float MinValue = 0.0f;
        float MaxValue = 1.0f;
        bool IsHovered = false;
        bool IsDragging = false;
    };

    struct UIToggleComponent {
        bool Enabled = true;
        bool IsOn = false;
        bool IsHovered = false;
        bool WasToggled = false;
    };

    struct UIInputFieldComponent {
        bool Enabled = true;
        std::string Text;
        std::string Placeholder = "Enter text...";
        bool IsFocused = false;
        bool IsHovered = false;
    };

    struct UIDocumentReferenceComponent {
        bool Enabled = true;
        AssetRef Document;
        bool InstantiateOnPlay = true;
        bool Instantiated = false;
    };

    // Atlas-based sprite animation (UV sub-rects) -- alternative to fixed-frame flipbook.
    struct SpriteSheetAnimatorComponent {
        bool Enabled = true;
        AssetRef Texture;
        int Columns = 4;
        int Rows = 4;
        float FrameRate = 8.0f;
        bool Loop = true;
        bool Playing = true;
        float ElapsedTime = 0.0f;
        int CurrentFrame = 0;
    };

    // Grid tilemap -- tile indices stored in external .tilemap asset.
    struct TilemapComponent {
        bool Enabled = true;
        int GridWidth = 16;
        int GridHeight = 16;
        glm::vec2 CellSize{ 16.0f, 16.0f };
        AssetRef Tileset;
        AssetRef TileData;
        int SortOrder = 0;
    };

    struct LineRendererComponent {
        bool Enabled = true;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float Width = 2.0f;
        bool Loop = false;
        int PointCount = 2;
        glm::vec3 Point0{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Point1{ 16.0f, 0.0f, 0.0f };
        glm::vec3 Point2{}, Point3{}, Point4{}, Point5{}, Point6{}, Point7{};
    };

    struct FollowTargetComponent {
        bool Enabled = true;
        EntityRef Target;
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        float SmoothSpeed = 0.0f; // 0 = snap each frame
        bool FollowX = true;
        bool FollowY = true;
        bool FollowZ = false;
    };

    struct AudioListenerComponent {
        bool Enabled = true;
    };

    // CPU particle emitter -- devkitPro-style simple burst/continuous particles.
    struct ParticleSystemComponent {
        bool Enabled = true;
        AssetRef Texture;
        bool Playing = true;
        bool Loop = true;
        float EmissionRate = 20.0f;
        int MaxParticles = 64;
        float Lifetime = 1.5f;
        float StartSpeed = 40.0f;
        float StartSize = 8.0f;
        glm::vec4 StartColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 EndColor{ 1.0f, 1.0f, 1.0f, 0.0f };
        glm::vec2 Gravity{ 0.0f, 60.0f };
        glm::vec2 VelocitySpread{ 30.0f, 30.0f };
        int SortOrder = 0;

        float EmissionAccumulator = 0.0f;
        int AliveCount = 0;
    };

    struct Particle {
        glm::vec2 Position;
        glm::vec2 Velocity;
        float Life = 0.0f;
        float MaxLife = 1.0f;
        float Size = 8.0f;
        glm::vec4 Color{ 1.0f };
    };

    inline constexpr int MaxParticlePoolSize = 256;

    inline glm::vec2& GetPolygonVertex2D(PolygonCollider2DComponent& poly, int index) {
        switch (index) {
            case 0: return poly.Vertex0;
            case 1: return poly.Vertex1;
            case 2: return poly.Vertex2;
            case 3: return poly.Vertex3;
            case 4: return poly.Vertex4;
            case 5: return poly.Vertex5;
            case 6: return poly.Vertex6;
            default: return poly.Vertex7;
        }
    }

    inline glm::vec3& GetLinePoint(LineRendererComponent& line, int index) {
        switch (index) {
            case 0: return line.Point0;
            case 1: return line.Point1;
            case 2: return line.Point2;
            case 3: return line.Point3;
            case 4: return line.Point4;
            case 5: return line.Point5;
            case 6: return line.Point6;
            default: return line.Point7;
        }
    }

}
