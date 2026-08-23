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
            MakeColorField("Color", &MeshRendererComponent::Color),
            MakeField("Texture", &MeshRendererComponent::Texture),
        });

        TypeRegistry::Register<ScreenGroupComponent>("Screen Group", false, {
            MakeField("Screen", &ScreenGroupComponent::Screen),
        });

        TypeRegistry::Register<Rigidbody2DComponent>("Rigidbody 2D", false, {
            MakeField("Is Static", &Rigidbody2DComponent::IsStatic),
            MakeField("Fixed Rotation", &Rigidbody2DComponent::FixedRotation),
        });

        TypeRegistry::Register<BoxCollider2DComponent>("Box Collider 2D", false, {
            MakeField("Offset", &BoxCollider2DComponent::Offset),
            MakeField("Size", &BoxCollider2DComponent::Size),
            MakeField("Density", &BoxCollider2DComponent::Density),
            MakeField("Friction", &BoxCollider2DComponent::Friction),
            MakeField("Restitution", &BoxCollider2DComponent::Restitution),
        });

        TypeRegistry::Register<CircleCollider2DComponent>("Circle Collider 2D", false, {
            MakeField("Offset", &CircleCollider2DComponent::Offset),
            MakeField("Radius", &CircleCollider2DComponent::Radius),
            MakeField("Density", &CircleCollider2DComponent::Density),
            MakeField("Friction", &CircleCollider2DComponent::Friction),
            MakeField("Restitution", &CircleCollider2DComponent::Restitution),
        });

        // Per-script public fields (Inspector-editable, like Unity's
        // [SerializeField]) need an Overrides map so Edit-mode edits survive
        // Play/Stop -- not implemented yet, only which class is attached.
        TypeRegistry::Register<BehaviourComponent>("Behaviour", false, {
            MakeField("Class", &BehaviourComponent::ClassName),
        });
    }

}
