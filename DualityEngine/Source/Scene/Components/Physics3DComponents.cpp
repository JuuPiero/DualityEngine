// Registration of Scene/Components/Physics3D.h. Display and field names are serialized: treat
// them as a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    namespace {
        const char* const kBreakTip = "0 = Infinity (never breaks). Exceeding it destroys the joint and sends OnJointBreak.";

        template<typename D>
        std::vector<FieldHandle> Joint3DFields() {
            return {
                BaseField<D>("Enabled", &Joint3DBase::Enabled),
                Tip(BaseField<D>("Connected Body", &Joint3DBase::ConnectedBody),
                    "Entity with a Rigidbody 3D. Empty = attached to a fixed point in the world."),
                Tip(BaseField<D>("Anchor", &Joint3DBase::Anchor), "Local to this body."),
                BaseField<D>("Auto Configure Connected Anchor", &Joint3DBase::AutoConfigureConnectedAnchor),
                BaseField<D>("Connected Anchor", &Joint3DBase::ConnectedAnchor),
                BaseField<D>("Enable Collision", &Joint3DBase::EnableCollision),
                Tip(BaseField<D>("Break Force", &Joint3DBase::BreakForce), kBreakTip),
                Tip(BaseField<D>("Break Torque", &Joint3DBase::BreakTorque), kBreakTip),
            };
        }

        // A Joint3DBase-derived component: the common joint fields followed by its own.
        template<typename D>
        void Joint3D(const char* displayName, std::vector<FieldHandle> own) {
            std::vector<FieldHandle> fields = Joint3DFields<D>();
            Append(fields, std::move(own));
            Component<D>(displayName, ComponentCategory::Physics, std::move(fields));
        }
    }

    void RegisterPhysics3DComponents() {
        Component<PhysicsRaycaster3DComponent>("Physics Raycaster 3D", ComponentCategory::Physics, {
            MakeField("Enabled", &PhysicsRaycaster3DComponent::Enabled),
            MakeField("Max Distance", &PhysicsRaycaster3DComponent::MaxDistance),
        });

        Component<Rigidbody3DComponent>("Rigidbody 3D", ComponentCategory::Physics, {
            MakeField("Enabled", &Rigidbody3DComponent::Enabled),
            MakeField("Body Type", &Rigidbody3DComponent::Type),
            MakeField("Mass", &Rigidbody3DComponent::Mass),
            MakeField("Linear Drag", &Rigidbody3DComponent::LinearDrag),
            MakeField("Angular Drag", &Rigidbody3DComponent::AngularDrag),
            MakeField("Gravity Scale", &Rigidbody3DComponent::GravityScale),
            MakeField("Use Gravity", &Rigidbody3DComponent::UseGravity),
        });

        Component<BoxCollider3DComponent>("Box Collider 3D", ComponentCategory::Physics, {
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

        Component<SphereCollider3DComponent>("Sphere Collider 3D", ComponentCategory::Physics, {
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

        Component<CapsuleCollider3DComponent>("Capsule Collider 3D", ComponentCategory::Physics, {
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

        // ---- Joints -----------------------------------------------------------------------
        Joint3D<FixedJointComponent>("Fixed Joint", {});
        Joint3D<HingeJointComponent>("Hinge Joint", {
            Tip(MakeField("Axis", &HingeJointComponent::Axis), "Rotation axis, local to this body."),
            Header(MakeField("Use Spring", &HingeJointComponent::UseSpring), "Spring"),
            MakeField("Spring", &HingeJointComponent::Spring),
            MakeField("Damper", &HingeJointComponent::Damper),
            Tip(MakeField("Target Position", &HingeJointComponent::TargetPosition), "Degrees."),
            Header(MakeField("Use Motor", &HingeJointComponent::UseMotor), "Motor"),
            Tip(MakeField("Target Velocity", &HingeJointComponent::MotorTargetVelocity), "Degrees per second."),
            MakeField("Force", &HingeJointComponent::MotorForce),
            MakeField("Free Spin", &HingeJointComponent::FreeSpin),
            Header(MakeField("Use Limits", &HingeJointComponent::UseLimits), "Limits"),
            Ranged(MakeField("Min", &HingeJointComponent::MinLimit), -180.0f, 180.0f),
            Ranged(MakeField("Max", &HingeJointComponent::MaxLimit), -180.0f, 180.0f),
            Ranged(MakeField("Bounciness", &HingeJointComponent::Bounciness), 0.0f, 1.0f),
        });
        Joint3D<SpringJointComponent>("Spring Joint", {
            MakeField("Spring", &SpringJointComponent::Spring),
            MakeField("Damper", &SpringJointComponent::Damper),
            Tip(MakeField("Min Distance", &SpringJointComponent::MinDistance), "Both 0 = the anchors' distance at Play start."),
            MakeField("Max Distance", &SpringJointComponent::MaxDistance),
            MakeField("Tolerance", &SpringJointComponent::Tolerance),
        });
        Joint3D<CharacterJointComponent>("Character Joint", {
            Tip(MakeField("Axis", &CharacterJointComponent::Axis), "Twist axis, local to this body."),
            MakeField("Swing Axis", &CharacterJointComponent::SwingAxis),
            Ranged(MakeField("Low Twist Limit", &CharacterJointComponent::LowTwistLimit), -180.0f, 180.0f),
            Ranged(MakeField("High Twist Limit", &CharacterJointComponent::HighTwistLimit), -180.0f, 180.0f),
            Ranged(MakeField("Swing 1 Limit", &CharacterJointComponent::Swing1Limit), 0.0f, 180.0f),
            Ranged(MakeField("Swing 2 Limit", &CharacterJointComponent::Swing2Limit), 0.0f, 180.0f),
        });

        Component<MeshColliderComponent>("Mesh Collider", ComponentCategory::Physics, {
            MakeField("Enabled", &MeshColliderComponent::Enabled),
            MeshAsset(Tip(MakeField("Mesh", &MeshColliderComponent::Mesh), "Empty = the Mesh Filter's mesh.")),
            Tip(MakeField("Convex", &MeshColliderComponent::Convex), "Required for a Dynamic Rigidbody 3D (convex hull)."),
            MakeField("Is Trigger", &MeshColliderComponent::IsTrigger),
            MakeField("Density", &MeshColliderComponent::Density),
            MakeField("Friction", &MeshColliderComponent::Friction),
            MakeField("Restitution", &MeshColliderComponent::Restitution),
            MakeField("Physics Material", &MeshColliderComponent::PhysicsMaterial),
        });

        Component<CharacterControllerComponent>("Character Controller", ComponentCategory::Physics, {
            MakeField("Enabled", &CharacterControllerComponent::Enabled),
            Ranged(MakeField("Slope Limit", &CharacterControllerComponent::SlopeLimit), 0.0f, 180.0f, "Steepest walkable slope in degrees."),
            Tip(MakeField("Step Offset", &CharacterControllerComponent::StepOffset), "Highest step climbed automatically."),
            MakeField("Skin Width", &CharacterControllerComponent::SkinWidth),
            MakeField("Min Move Distance", &CharacterControllerComponent::MinMoveDistance),
            MakeField("Center", &CharacterControllerComponent::Center),
            MakeField("Radius", &CharacterControllerComponent::Radius),
            Tip(MakeField("Height", &CharacterControllerComponent::Height), "Total height including both caps."),
        });

        Component<ConstantForceComponent>("Constant Force", ComponentCategory::Physics, {
            MakeField("Enabled", &ConstantForceComponent::Enabled),
            MakeField("Force", &ConstantForceComponent::Force),
            MakeField("Relative Force", &ConstantForceComponent::RelativeForce),
            MakeField("Torque", &ConstantForceComponent::Torque),
            MakeField("Relative Torque", &ConstantForceComponent::RelativeTorque),
        });
    }

}
