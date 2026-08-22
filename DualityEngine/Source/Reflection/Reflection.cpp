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
        });

        TypeRegistry::Register<CameraComponent>("Camera", false, {
            MakeField("Screen", &CameraComponent::Screen),
            MakeField("Primary", &CameraComponent::Primary),
            MakeField("Zoom", &CameraComponent::Zoom),
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
