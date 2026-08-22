#include "DualityEngine/Reflection/Reflection.h"

#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    void RegisterBuiltinComponents() {
        if (!TypeRegistry::All().empty())
            return;

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
        });

        // Per-script public fields (Inspector-editable, like Unity's
        // [SerializeField]) need an Overrides map so Edit-mode edits survive
        // Play/Stop -- not implemented yet, only which class is attached.
        TypeRegistry::Register<BehaviourComponent>("Behaviour", false, {
            MakeField("Class", &BehaviourComponent::ClassName),
        });
    }

}
