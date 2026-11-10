// Registration of Scene/Components/Constraints.h. Display and field names are serialized: treat
// them as a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterConstraintComponents() {
        Component<FollowTargetComponent>("Follow Target", ComponentCategory::Miscellaneous, {
            MakeField("Enabled", &FollowTargetComponent::Enabled),
            MakeField("Target", &FollowTargetComponent::Target),
            MakeField("Offset", &FollowTargetComponent::Offset),
            MakeField("Smooth Speed", &FollowTargetComponent::SmoothSpeed),
            MakeField("Follow X", &FollowTargetComponent::FollowX),
            MakeField("Follow Y", &FollowTargetComponent::FollowY),
            MakeField("Follow Z", &FollowTargetComponent::FollowZ),
        });

        const char* const constraintTip = "Evaluated every frame in Play mode after LateUpdate.";
        Component<PositionConstraintComponent>("Position Constraint", ComponentCategory::Miscellaneous, {
            Tip(MakeField("Enabled", &PositionConstraintComponent::Enabled), constraintTip),
            Ranged(MakeField("Weight", &PositionConstraintComponent::Weight), 0.0f, 1.0f),
            MakeField("Sources", &PositionConstraintComponent::Sources),
            MakeField("Position At Rest", &PositionConstraintComponent::TranslationAtRest),
            MakeField("Position Offset", &PositionConstraintComponent::TranslationOffset),
            MakeField("Constrain X", &PositionConstraintComponent::ConstrainX),
            MakeField("Constrain Y", &PositionConstraintComponent::ConstrainY),
            MakeField("Constrain Z", &PositionConstraintComponent::ConstrainZ),
        });

        Component<RotationConstraintComponent>("Rotation Constraint", ComponentCategory::Miscellaneous, {
            Tip(MakeField("Enabled", &RotationConstraintComponent::Enabled), constraintTip),
            Ranged(MakeField("Weight", &RotationConstraintComponent::Weight), 0.0f, 1.0f),
            MakeField("Sources", &RotationConstraintComponent::Sources),
            MakeField("Rotation At Rest", &RotationConstraintComponent::RotationAtRest),
            MakeField("Rotation Offset", &RotationConstraintComponent::RotationOffset),
            MakeField("Constrain X", &RotationConstraintComponent::ConstrainX),
            MakeField("Constrain Y", &RotationConstraintComponent::ConstrainY),
            MakeField("Constrain Z", &RotationConstraintComponent::ConstrainZ),
        });

        Component<ScaleConstraintComponent>("Scale Constraint", ComponentCategory::Miscellaneous, {
            Tip(MakeField("Enabled", &ScaleConstraintComponent::Enabled), constraintTip),
            Ranged(MakeField("Weight", &ScaleConstraintComponent::Weight), 0.0f, 1.0f),
            MakeField("Sources", &ScaleConstraintComponent::Sources),
            MakeField("Scale At Rest", &ScaleConstraintComponent::ScaleAtRest),
            MakeField("Scale Offset", &ScaleConstraintComponent::ScaleOffset),
            MakeField("Constrain X", &ScaleConstraintComponent::ConstrainX),
            MakeField("Constrain Y", &ScaleConstraintComponent::ConstrainY),
            MakeField("Constrain Z", &ScaleConstraintComponent::ConstrainZ),
        });

        Component<ParentConstraintComponent>("Parent Constraint", ComponentCategory::Miscellaneous, {
            Tip(MakeField("Enabled", &ParentConstraintComponent::Enabled), constraintTip),
            Ranged(MakeField("Weight", &ParentConstraintComponent::Weight), 0.0f, 1.0f),
            MakeField("Source", &ParentConstraintComponent::Source),
            Tip(MakeField("Auto Offset", &ParentConstraintComponent::AutoOffset), "Capture the pose relative to Source when Play starts."),
            MakeField("Position Offset", &ParentConstraintComponent::TranslationOffset),
            MakeField("Rotation Offset", &ParentConstraintComponent::RotationOffset),
            MakeField("Constrain Position", &ParentConstraintComponent::ConstrainTranslation),
            MakeField("Constrain Rotation", &ParentConstraintComponent::ConstrainRotation),
        });

        Component<LookAtConstraintComponent>("Look At Constraint", ComponentCategory::Miscellaneous, {
            Tip(MakeField("Enabled", &LookAtConstraintComponent::Enabled), "Points the entity's forward (-Z, like a Camera) at Source."),
            Ranged(MakeField("Weight", &LookAtConstraintComponent::Weight), 0.0f, 1.0f),
            MakeField("Source", &LookAtConstraintComponent::Source),
            Ranged(MakeField("Roll", &LookAtConstraintComponent::Roll), -180.0f, 180.0f),
            MakeField("Use Up Object", &LookAtConstraintComponent::UseUpObject),
            MakeField("World Up Object", &LookAtConstraintComponent::WorldUpObject),
            MakeField("Rotation Offset", &LookAtConstraintComponent::RotationOffset),
        });

        Component<AimConstraintComponent>("Aim Constraint", ComponentCategory::Miscellaneous, {
            Tip(MakeField("Enabled", &AimConstraintComponent::Enabled), "2D: Aim Vector (1,0,0) + World Up Type None."),
            Ranged(MakeField("Weight", &AimConstraintComponent::Weight), 0.0f, 1.0f),
            MakeField("Sources", &AimConstraintComponent::Sources),
            MakeField("Aim Vector", &AimConstraintComponent::AimVector),
            MakeField("Up Vector", &AimConstraintComponent::UpVector),
            MakeEnumField("World Up Type", &AimConstraintComponent::WorldUpType, { "Scene Up", "Object Up", "Object Rotation Up", "Vector", "None" }),
            MakeField("World Up Vector", &AimConstraintComponent::WorldUpVector),
            MakeField("World Up Object", &AimConstraintComponent::WorldUpObject),
            MakeField("Rotation Offset", &AimConstraintComponent::RotationOffset),
        });
    }

}
