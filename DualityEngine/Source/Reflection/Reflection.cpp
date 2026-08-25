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
            MakeColorField("Color", &SpriteRendererComponent::Color),
            MakeField("Size", &SpriteRendererComponent::Size),
            MakeField("Texture", &SpriteRendererComponent::Texture),
        });

        TypeRegistry::Register<SpriteFlipbookComponent>("Sprite Flipbook", false, {
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
            MakeField("Screen", &CameraComponent::Screen),
            MakeField("Primary", &CameraComponent::Primary),
            MakeField("Zoom", &CameraComponent::Zoom),
            MakeField("Projection", &CameraComponent::Projection),
            MakeField("Fov Degrees", &CameraComponent::FovDegrees),
            MakeField("Near Plane", &CameraComponent::NearPlane),
            MakeField("Far Plane", &CameraComponent::FarPlane),
        });

        TypeRegistry::Register<MeshRendererComponent>("Mesh Renderer", false, {
            MakeField("Primitive", &MeshRendererComponent::Primitive),
            MakeField("Material", &MeshRendererComponent::Material),
            MakeField("Mesh", &MeshRendererComponent::Mesh),
        });

        TypeRegistry::Register<ScreenGroupComponent>("Screen Group", false, {
            MakeField("Screen", &ScreenGroupComponent::Screen),
        });

        TypeRegistry::Register<UIRectComponent>("UI Rect", false, {
            MakeField("Screen", &UIRectComponent::Screen),
            MakeField("Anchor", &UIRectComponent::Anchor),
            MakeField("Offset", &UIRectComponent::Offset),
            MakeField("Size", &UIRectComponent::Size),
        });

        TypeRegistry::Register<UIImageComponent>("UI Image", false, {
            MakeColorField("Color", &UIImageComponent::Color),
            MakeField("Texture", &UIImageComponent::Texture),
        });

        // IsHovered/IsPressed/WasClicked deliberately not registered -- runtime-only state,
        // same convention as SpriteFlipbookComponent::CurrentFrame not being an authored field.
        TypeRegistry::Register<UIButtonComponent>("UI Button", false, {
            MakeColorField("Normal Color", &UIButtonComponent::NormalColor),
            MakeColorField("Hover Color", &UIButtonComponent::HoverColor),
            MakeColorField("Pressed Color", &UIButtonComponent::PressedColor),
        });

        TypeRegistry::Register<Rigidbody2DComponent>("Rigidbody 2D", false, {
            MakeField("Body Type", &Rigidbody2DComponent::Type),
            MakeField("Fixed Rotation", &Rigidbody2DComponent::FixedRotation),
        });

        TypeRegistry::Register<BoxCollider2DComponent>("Box Collider 2D", false, {
            MakeField("Offset", &BoxCollider2DComponent::Offset),
            MakeField("Size", &BoxCollider2DComponent::Size),
            MakeField("Density", &BoxCollider2DComponent::Density),
            MakeField("Friction", &BoxCollider2DComponent::Friction),
            MakeField("Restitution", &BoxCollider2DComponent::Restitution),
            MakeField("Is Trigger", &BoxCollider2DComponent::IsTrigger),
        });

        TypeRegistry::Register<CircleCollider2DComponent>("Circle Collider 2D", false, {
            MakeField("Offset", &CircleCollider2DComponent::Offset),
            MakeField("Radius", &CircleCollider2DComponent::Radius),
            MakeField("Density", &CircleCollider2DComponent::Density),
            MakeField("Friction", &CircleCollider2DComponent::Friction),
            MakeField("Restitution", &CircleCollider2DComponent::Restitution),
            MakeField("Is Trigger", &CircleCollider2DComponent::IsTrigger),
        });

        TypeRegistry::Register<Rigidbody3DComponent>("Rigidbody 3D", false, {
            MakeField("Body Type", &Rigidbody3DComponent::Type),
        });

        TypeRegistry::Register<BoxCollider3DComponent>("Box Collider 3D", false, {
            MakeField("Offset", &BoxCollider3DComponent::Offset),
            MakeField("Size", &BoxCollider3DComponent::Size),
            MakeField("Density", &BoxCollider3DComponent::Density),
            MakeField("Friction", &BoxCollider3DComponent::Friction),
            MakeField("Restitution", &BoxCollider3DComponent::Restitution),
            MakeField("Is Trigger", &BoxCollider3DComponent::IsTrigger),
        });

        TypeRegistry::Register<SphereCollider3DComponent>("Sphere Collider 3D", false, {
            MakeField("Offset", &SphereCollider3DComponent::Offset),
            MakeField("Radius", &SphereCollider3DComponent::Radius),
            MakeField("Density", &SphereCollider3DComponent::Density),
            MakeField("Friction", &SphereCollider3DComponent::Friction),
            MakeField("Restitution", &SphereCollider3DComponent::Restitution),
            MakeField("Is Trigger", &SphereCollider3DComponent::IsTrigger),
        });

        // Per-script public fields (Inspector-editable, like Unity's
        // [SerializeField]) need an Overrides map so Edit-mode edits survive
        // Play/Stop -- not implemented yet, only which class is attached.
        TypeRegistry::Register<BehaviourComponent>("Behaviour", false, {
            MakeField("Class", &BehaviourComponent::ClassName),
        });
    }

}
