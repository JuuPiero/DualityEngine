// Registration of Scene/Components/Animation.h. Display and field names are serialized: treat
// them as a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterAnimationComponents() {
        Component<SpriteFlipbookComponent>("Sprite Flipbook", ComponentCategory::Animation, {
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

        Component<SpriteSheetAnimatorComponent>("Sprite Sheet Animator", ComponentCategory::Animation, {
            MakeField("Enabled", &SpriteSheetAnimatorComponent::Enabled),
            MakeField("Texture", &SpriteSheetAnimatorComponent::Texture),
            MakeField("Columns", &SpriteSheetAnimatorComponent::Columns),
            MakeField("Rows", &SpriteSheetAnimatorComponent::Rows),
            MakeField("Frame Rate", &SpriteSheetAnimatorComponent::FrameRate),
            MakeField("Loop", &SpriteSheetAnimatorComponent::Loop),
            MakeField("Playing", &SpriteSheetAnimatorComponent::Playing),
        });

        Component<AnimationComponent>("Animation", ComponentCategory::Animation, {
            MakeField("Enabled", &AnimationComponent::Enabled),
            MakeField("Clip", &AnimationComponent::Clip),
            MakeField("Play On Awake", &AnimationComponent::PlayOnAwake),
            MakeField("Loop", &AnimationComponent::Loop),
            MakeField("Speed", &AnimationComponent::Speed),
            MakeField("Playing", &AnimationComponent::Playing),
        });
    }

}
