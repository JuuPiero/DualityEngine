#include "DualityEngine/Reflection/Reflection.h"

#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    void RegisterBuiltinComponents() {
        if (!TypeRegistry::All().empty())
            return;

        TypeRegistry::Register<NameComponent>("Name", true, {
            MakeField("Name", &NameComponent::Name),
        });

        TypeRegistry::Register<TagComponent>("Tag", true, {
            MakeField("Tag", &TagComponent::Tag),
        });

        TypeRegistry::Register<ActiveComponent>("Active", true, {
            MakeField("Active", &ActiveComponent::Active),
        });

        TypeRegistry::Register<TransformComponent>("Transform", true, {
            MakeField("Translation", &TransformComponent::Translation),
            MakeField("Rotation", &TransformComponent::Rotation),
            MakeField("Scale", &TransformComponent::Scale),
        });

        TypeRegistry::Register<SpriteRendererComponent>("Sprite Renderer", false, {
            MakeField("Enabled", &SpriteRendererComponent::Enabled),
            MakeColorField("Color", &SpriteRendererComponent::Color),
            MakeField("Size", &SpriteRendererComponent::Size),
            MakeField("Texture", &SpriteRendererComponent::Texture),
            MakeField("Sort Order", &SpriteRendererComponent::SortOrder),
            MakeField("Flip X", &SpriteRendererComponent::FlipX),
            MakeField("Flip Y", &SpriteRendererComponent::FlipY),
            MakeField("Pivot", &SpriteRendererComponent::Pivot),
            MakeField("Slice Border", &SpriteRendererComponent::SliceBorder),
        });

        TypeRegistry::Register<SpriteFlipbookComponent>("Sprite Flipbook", false, {
            MakeField("Enabled", &SpriteFlipbookComponent::Enabled),
            MakeField("Frame 0", &SpriteFlipbookComponent::Frame0),
            MakeField("Frame 1", &SpriteFlipbookComponent::Frame1),
            MakeField("Frame 2", &SpriteFlipbookComponent::Frame2),
            MakeField("Frame 3", &SpriteFlipbookComponent::Frame3),
            MakeField("Frame 4", &SpriteFlipbookComponent::Frame4),
            MakeField("Frame 5", &SpriteFlipbookComponent::Frame5),
            MakeField("Frame 6", &SpriteFlipbookComponent::Frame6),
            MakeField("Frame 7", &SpriteFlipbookComponent::Frame7),
            MakeField("Frame Duration", &SpriteFlipbookComponent::FrameDuration),
            MakeField("Loop", &SpriteFlipbookComponent::Loop),
            MakeField("Playing", &SpriteFlipbookComponent::Playing),
        });

        TypeRegistry::Register<CameraComponent>("Camera", false, {
            MakeField("Enabled", &CameraComponent::Enabled),
            MakeField("Screen", &CameraComponent::Screen),
            MakeField("Primary", &CameraComponent::Primary),
            MakeField("Zoom", &CameraComponent::Zoom),
            MakeField("Projection", &CameraComponent::Projection),
            MakeField("Fov Degrees", &CameraComponent::FovDegrees),
            MakeField("Near Plane", &CameraComponent::NearPlane),
            MakeField("Far Plane", &CameraComponent::FarPlane),
            MakeColorField("Background", &CameraComponent::Background),
            MakeField("Culling Mask", &CameraComponent::CullingMask),
        });
        if (ComponentTypeInfo* cameraType = TypeRegistry::Find("Camera")) {
            cameraType->AddDefault = [](Entity entity) {
                if (!entity.HasComponent<CameraComponent>())
                    entity.AddComponent<CameraComponent>();
                auto& camera = entity.GetComponent<CameraComponent>();
                if (camera.Projection == ProjectionType::Orthographic) {
                    if (!entity.HasComponent<PhysicsRaycaster2DComponent>())
                        entity.AddComponent<PhysicsRaycaster2DComponent>();
                } else {
                    if (!entity.HasComponent<PhysicsRaycaster3DComponent>())
                        entity.AddComponent<PhysicsRaycaster3DComponent>();
                }
            };
        }

        TypeRegistry::Register<PhysicsRaycaster2DComponent>("Physics Raycaster 2D", false, {
            MakeField("Enabled", &PhysicsRaycaster2DComponent::Enabled),
        });

        TypeRegistry::Register<PhysicsRaycaster3DComponent>("Physics Raycaster 3D", false, {
            MakeField("Enabled", &PhysicsRaycaster3DComponent::Enabled),
            MakeField("Max Distance", &PhysicsRaycaster3DComponent::MaxDistance),
        });

        TypeRegistry::Register<MeshRendererComponent>("Mesh Renderer", false, {
            MakeField("Enabled", &MeshRendererComponent::Enabled),
            MakeField("Primitive", &MeshRendererComponent::Primitive),
            MakeField("Materials", &MeshRendererComponent::Materials),
            MakeField("Mesh", &MeshRendererComponent::Mesh),
            MakeField("Sort Order", &MeshRendererComponent::SortOrder),
        });

        TypeRegistry::Register<LayerComponent>("Layer", false, {
            MakeField("Layer", &LayerComponent::Value),
        });

        TypeRegistry::Register<AudioSourceComponent>("Audio Source", false, {
            MakeField("Enabled", &AudioSourceComponent::Enabled),
            MakeField("Clip", &AudioSourceComponent::Clip),
            MakeField("Loop", &AudioSourceComponent::Loop),
            MakeField("Volume", &AudioSourceComponent::Volume),
            MakeField("Play On Awake", &AudioSourceComponent::PlayOnAwake),
            MakeField("Mute", &AudioSourceComponent::Mute),
            MakeField("Spatial Blend", &AudioSourceComponent::SpatialBlend),
            MakeField("Min Distance", &AudioSourceComponent::MinDistance),
            MakeField("Max Distance", &AudioSourceComponent::MaxDistance),
        });

        TypeRegistry::Register<AudioListenerComponent>("Audio Listener", false, {
            MakeField("Enabled", &AudioListenerComponent::Enabled),
        });

        TypeRegistry::Register<UIRectComponent>("UI Rect", false, {
            MakeField("Enabled", &UIRectComponent::Enabled),
            MakeField("Screen", &UIRectComponent::Screen),
            MakeField("Anchor Min", &UIRectComponent::AnchorMin),
            MakeField("Anchor Max", &UIRectComponent::AnchorMax),
            MakeField("Pivot", &UIRectComponent::Pivot),
            MakeField("Anchored Position", &UIRectComponent::AnchoredPosition),
            MakeField("Size Delta", &UIRectComponent::SizeDelta),
            MakeField("Sort Order", &UIRectComponent::SortOrder),
        });

        TypeRegistry::Register<CanvasComponent>("Canvas", false, {
            MakeField("Enabled", &CanvasComponent::Enabled),
            MakeField("Render Mode", &CanvasComponent::RenderMode),
            MakeField("Sort Order", &CanvasComponent::SortOrder),
            MakeField("Scale Factor", &CanvasComponent::ScaleFactor),
            MakeField("Target Camera", &CanvasComponent::TargetCamera),
        });

        TypeRegistry::Register<UILayoutGroupComponent>("UI Layout Group", false, {
            MakeField("Enabled", &UILayoutGroupComponent::Enabled),
            MakeField("Layout", &UILayoutGroupComponent::Layout),
            MakeField("Spacing", &UILayoutGroupComponent::Spacing),
            MakeField("Padding", &UILayoutGroupComponent::Padding),
            MakeField("Control Width", &UILayoutGroupComponent::ChildControlWidth),
            MakeField("Control Height", &UILayoutGroupComponent::ChildControlHeight),
        });

        TypeRegistry::Register<UIImageComponent>("UI Image", false, {
            MakeField("Enabled", &UIImageComponent::Enabled),
            MakeColorField("Color", &UIImageComponent::Color),
            MakeField("Texture", &UIImageComponent::Texture),
            MakeField("Slice Border", &UIImageComponent::SliceBorder),
        });

        // IsHovered/IsPressed/WasClicked deliberately not registered -- runtime-only state,
        // same convention as SpriteFlipbookComponent::CurrentFrame not being an authored field.
        TypeRegistry::Register<UIButtonComponent>("UI Button", false, {
            MakeField("Enabled", &UIButtonComponent::Enabled),
            MakeColorField("Normal Color", &UIButtonComponent::NormalColor),
            MakeColorField("Hover Color", &UIButtonComponent::HoverColor),
            MakeColorField("Pressed Color", &UIButtonComponent::PressedColor),
        });

        TypeRegistry::Register<UITextComponent>("UI Text", false, {
            MakeField("Enabled", &UITextComponent::Enabled),
            MakeField("Text", &UITextComponent::Text),
            MakeField("Font", &UITextComponent::Font),
            MakeField("Font Size", &UITextComponent::FontSize),
            MakeField("Color", &UITextComponent::Color),
            MakeField("Alignment", &UITextComponent::Alignment),
        });

        TypeRegistry::Register<UISliderComponent>("UI Slider", false, {
            MakeField("Enabled", &UISliderComponent::Enabled),
            MakeField("Value", &UISliderComponent::Value),
            MakeField("Min Value", &UISliderComponent::MinValue),
            MakeField("Max Value", &UISliderComponent::MaxValue),
        });

        TypeRegistry::Register<UIToggleComponent>("UI Toggle", false, {
            MakeField("Enabled", &UIToggleComponent::Enabled),
            MakeField("Is On", &UIToggleComponent::IsOn),
        });

        TypeRegistry::Register<UIInputFieldComponent>("UI Input Field", false, {
            MakeField("Enabled", &UIInputFieldComponent::Enabled),
            MakeField("Text", &UIInputFieldComponent::Text),
            MakeField("Placeholder", &UIInputFieldComponent::Placeholder),
        });

        TypeRegistry::Register<UIDocumentReferenceComponent>("UI Document Reference", false, {
            MakeField("Enabled", &UIDocumentReferenceComponent::Enabled),
            MakeField("Document", &UIDocumentReferenceComponent::Document),
            MakeField("Instantiate On Play", &UIDocumentReferenceComponent::InstantiateOnPlay),
        });

        TypeRegistry::Register<SpriteSheetAnimatorComponent>("Sprite Sheet Animator", false, {
            MakeField("Enabled", &SpriteSheetAnimatorComponent::Enabled),
            MakeField("Texture", &SpriteSheetAnimatorComponent::Texture),
            MakeField("Columns", &SpriteSheetAnimatorComponent::Columns),
            MakeField("Rows", &SpriteSheetAnimatorComponent::Rows),
            MakeField("Frame Rate", &SpriteSheetAnimatorComponent::FrameRate),
            MakeField("Loop", &SpriteSheetAnimatorComponent::Loop),
            MakeField("Playing", &SpriteSheetAnimatorComponent::Playing),
        });

        TypeRegistry::Register<TilemapComponent>("Tilemap", false, {
            MakeField("Enabled", &TilemapComponent::Enabled),
            MakeField("Grid Width", &TilemapComponent::GridWidth),
            MakeField("Grid Height", &TilemapComponent::GridHeight),
            MakeField("Cell Size", &TilemapComponent::CellSize),
            MakeField("Tileset", &TilemapComponent::Tileset),
            MakeField("Tile Data", &TilemapComponent::TileData),
            MakeField("Sort Order", &TilemapComponent::SortOrder),
        });

        TypeRegistry::Register<LineRendererComponent>("Line Renderer", false, {
            MakeField("Enabled", &LineRendererComponent::Enabled),
            MakeColorField("Color", &LineRendererComponent::Color),
            MakeField("Width", &LineRendererComponent::Width),
            MakeField("Loop", &LineRendererComponent::Loop),
            MakeField("Point Count", &LineRendererComponent::PointCount),
            MakeField("Point 0", &LineRendererComponent::Point0),
            MakeField("Point 1", &LineRendererComponent::Point1),
            MakeField("Point 2", &LineRendererComponent::Point2),
            MakeField("Point 3", &LineRendererComponent::Point3),
        });

        TypeRegistry::Register<FollowTargetComponent>("Follow Target", false, {
            MakeField("Enabled", &FollowTargetComponent::Enabled),
            MakeField("Target", &FollowTargetComponent::Target),
            MakeField("Offset", &FollowTargetComponent::Offset),
            MakeField("Smooth Speed", &FollowTargetComponent::SmoothSpeed),
            MakeField("Follow X", &FollowTargetComponent::FollowX),
            MakeField("Follow Y", &FollowTargetComponent::FollowY),
            MakeField("Follow Z", &FollowTargetComponent::FollowZ),
        });

        TypeRegistry::Register<ParticleSystemComponent>("Particle System", false, {
            MakeField("Enabled", &ParticleSystemComponent::Enabled),
            MakeField("Texture", &ParticleSystemComponent::Texture),
            MakeField("Playing", &ParticleSystemComponent::Playing),
            MakeField("Loop", &ParticleSystemComponent::Loop),
            MakeField("Emission Rate", &ParticleSystemComponent::EmissionRate),
            MakeField("Max Particles", &ParticleSystemComponent::MaxParticles),
            MakeField("Lifetime", &ParticleSystemComponent::Lifetime),
            MakeField("Start Speed", &ParticleSystemComponent::StartSpeed),
            MakeField("Start Size", &ParticleSystemComponent::StartSize),
            MakeColorField("Start Color", &ParticleSystemComponent::StartColor),
            MakeColorField("End Color", &ParticleSystemComponent::EndColor),
            MakeField("Gravity", &ParticleSystemComponent::Gravity),
            MakeField("Velocity Spread", &ParticleSystemComponent::VelocitySpread),
            MakeField("Sort Order", &ParticleSystemComponent::SortOrder),
        });

        TypeRegistry::Register<Rigidbody2DComponent>("Rigidbody 2D", false, {
            MakeField("Enabled", &Rigidbody2DComponent::Enabled),
            MakeField("Body Type", &Rigidbody2DComponent::Type),
            MakeField("Fixed Rotation", &Rigidbody2DComponent::FixedRotation),
            MakeField("Mass", &Rigidbody2DComponent::Mass),
            MakeField("Linear Drag", &Rigidbody2DComponent::LinearDrag),
            MakeField("Angular Drag", &Rigidbody2DComponent::AngularDrag),
            MakeField("Gravity Scale", &Rigidbody2DComponent::GravityScale),
            MakeField("Use Gravity", &Rigidbody2DComponent::UseGravity),
        });

        TypeRegistry::Register<BoxCollider2DComponent>("Box Collider 2D", false, {
            MakeField("Enabled", &BoxCollider2DComponent::Enabled),
            MakeField("Offset", &BoxCollider2DComponent::Offset),
            MakeField("Size", &BoxCollider2DComponent::Size),
            MakeField("Density", &BoxCollider2DComponent::Density),
            MakeField("Friction", &BoxCollider2DComponent::Friction),
            MakeField("Restitution", &BoxCollider2DComponent::Restitution),
            MakeField("Is Trigger", &BoxCollider2DComponent::IsTrigger),
            MakeField("Physics Material", &BoxCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &BoxCollider2DComponent::EditMode),
        });

        TypeRegistry::Register<CircleCollider2DComponent>("Circle Collider 2D", false, {
            MakeField("Enabled", &CircleCollider2DComponent::Enabled),
            MakeField("Offset", &CircleCollider2DComponent::Offset),
            MakeField("Radius", &CircleCollider2DComponent::Radius),
            MakeField("Density", &CircleCollider2DComponent::Density),
            MakeField("Friction", &CircleCollider2DComponent::Friction),
            MakeField("Restitution", &CircleCollider2DComponent::Restitution),
            MakeField("Is Trigger", &CircleCollider2DComponent::IsTrigger),
            MakeField("Physics Material", &CircleCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &CircleCollider2DComponent::EditMode),
        });

        TypeRegistry::Register<CapsuleCollider2DComponent>("Capsule Collider 2D", false, {
            MakeField("Enabled", &CapsuleCollider2DComponent::Enabled),
            MakeField("Offset", &CapsuleCollider2DComponent::Offset),
            MakeField("Radius", &CapsuleCollider2DComponent::Radius),
            MakeField("Height", &CapsuleCollider2DComponent::Height),
            MakeField("Density", &CapsuleCollider2DComponent::Density),
            MakeField("Friction", &CapsuleCollider2DComponent::Friction),
            MakeField("Restitution", &CapsuleCollider2DComponent::Restitution),
            MakeField("Is Trigger", &CapsuleCollider2DComponent::IsTrigger),
            MakeField("Physics Material", &CapsuleCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &CapsuleCollider2DComponent::EditMode),
        });

        TypeRegistry::Register<PolygonCollider2DComponent>("Polygon Collider 2D", false, {
            MakeField("Enabled", &PolygonCollider2DComponent::Enabled),
            MakeField("Offset", &PolygonCollider2DComponent::Offset),
            MakeField("Vertex Count", &PolygonCollider2DComponent::VertexCount),
            MakeField("Vertex 0", &PolygonCollider2DComponent::Vertex0),
            MakeField("Vertex 1", &PolygonCollider2DComponent::Vertex1),
            MakeField("Vertex 2", &PolygonCollider2DComponent::Vertex2),
            MakeField("Vertex 3", &PolygonCollider2DComponent::Vertex3),
            MakeField("Density", &PolygonCollider2DComponent::Density),
            MakeField("Friction", &PolygonCollider2DComponent::Friction),
            MakeField("Restitution", &PolygonCollider2DComponent::Restitution),
            MakeField("Is Trigger", &PolygonCollider2DComponent::IsTrigger),
            MakeField("Physics Material", &PolygonCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &PolygonCollider2DComponent::EditMode),
        });

        TypeRegistry::Register<Rigidbody3DComponent>("Rigidbody 3D", false, {
            MakeField("Enabled", &Rigidbody3DComponent::Enabled),
            MakeField("Body Type", &Rigidbody3DComponent::Type),
            MakeField("Mass", &Rigidbody3DComponent::Mass),
            MakeField("Linear Drag", &Rigidbody3DComponent::LinearDrag),
            MakeField("Angular Drag", &Rigidbody3DComponent::AngularDrag),
            MakeField("Gravity Scale", &Rigidbody3DComponent::GravityScale),
            MakeField("Use Gravity", &Rigidbody3DComponent::UseGravity),
        });

        TypeRegistry::Register<BoxCollider3DComponent>("Box Collider 3D", false, {
            MakeField("Enabled", &BoxCollider3DComponent::Enabled),
            MakeField("Offset", &BoxCollider3DComponent::Offset),
            MakeField("Size", &BoxCollider3DComponent::Size),
            MakeField("Density", &BoxCollider3DComponent::Density),
            MakeField("Friction", &BoxCollider3DComponent::Friction),
            MakeField("Restitution", &BoxCollider3DComponent::Restitution),
            MakeField("Is Trigger", &BoxCollider3DComponent::IsTrigger),
            MakeField("Physics Material", &BoxCollider3DComponent::PhysicsMaterial),
            MakeField("Edit", &BoxCollider3DComponent::EditMode),
        });

        TypeRegistry::Register<SphereCollider3DComponent>("Sphere Collider 3D", false, {
            MakeField("Enabled", &SphereCollider3DComponent::Enabled),
            MakeField("Offset", &SphereCollider3DComponent::Offset),
            MakeField("Radius", &SphereCollider3DComponent::Radius),
            MakeField("Density", &SphereCollider3DComponent::Density),
            MakeField("Friction", &SphereCollider3DComponent::Friction),
            MakeField("Restitution", &SphereCollider3DComponent::Restitution),
            MakeField("Is Trigger", &SphereCollider3DComponent::IsTrigger),
            MakeField("Physics Material", &SphereCollider3DComponent::PhysicsMaterial),
            MakeField("Edit", &SphereCollider3DComponent::EditMode),
        });

        TypeRegistry::Register<CapsuleCollider3DComponent>("Capsule Collider 3D", false, {
            MakeField("Enabled", &CapsuleCollider3DComponent::Enabled),
            MakeField("Offset", &CapsuleCollider3DComponent::Offset),
            MakeField("Radius", &CapsuleCollider3DComponent::Radius),
            MakeField("Height", &CapsuleCollider3DComponent::Height),
            MakeField("Density", &CapsuleCollider3DComponent::Density),
            MakeField("Friction", &CapsuleCollider3DComponent::Friction),
            MakeField("Restitution", &CapsuleCollider3DComponent::Restitution),
            MakeField("Is Trigger", &CapsuleCollider3DComponent::IsTrigger),
            MakeField("Physics Material", &CapsuleCollider3DComponent::PhysicsMaterial),
            MakeField("Edit", &CapsuleCollider3DComponent::EditMode),
        });

        // Registered with zero reflected fields -- BehaviourComponent now holds a vector of
        // script slots (BehaviourComponent::Scripts), not a single ClassName/PropertyOverrides
        // pair, so it can't ride the generic single-FieldValue-per-field TypeRegistry rendering
        // any more. Still registered so the Properties panel's generic type.Has(selected) check
        // finds it and draws its "Scripts" section, and so it's excluded from the flat
        // Add-Component list the same way any other 0-field marker type would be -- but
        // PropertiesPanel.cpp and EntitySerialization.cpp both special-case DisplayName ==
        // "Scripts" for the real per-slot rendering/serialization.
        TypeRegistry::Register<BehaviourComponent>("Scripts", false, {});
    }

}
