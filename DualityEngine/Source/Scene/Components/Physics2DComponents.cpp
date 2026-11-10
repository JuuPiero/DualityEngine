// Registration of Scene/Components/Physics2D.h. Display and field names are serialized: treat
// them as a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    namespace {
        const char* const kBreakTip = "0 = Infinity (never breaks). Exceeding it destroys the joint and sends OnJointBreak.";

        template<typename D>
        std::vector<FieldHandle> Joint2DFields(bool withConnectedBody = true) {
            std::vector<FieldHandle> fields;
            fields.push_back(BaseField<D>("Enabled", &Joint2DBase::Enabled));
            if (withConnectedBody) {
                fields.push_back(Tip(BaseField<D>("Connected Body", &Joint2DBase::ConnectedBody),
                    "Entity with a Rigidbody 2D. Empty = attached to a fixed point in the world."));
                fields.push_back(BaseField<D>("Enable Collision", &Joint2DBase::EnableCollision));
                fields.push_back(BaseField<D>("Auto Configure Connected Anchor", &Joint2DBase::AutoConfigureConnectedAnchor));
            }
            fields.push_back(Tip(BaseField<D>("Anchor", &Joint2DBase::Anchor), "Local to this body."));
            if (withConnectedBody)
                fields.push_back(Tip(BaseField<D>("Connected Anchor", &Joint2DBase::ConnectedAnchor),
                    "Local to the connected body, or a world point when there is none."));
            fields.push_back(Tip(BaseField<D>("Break Force", &Joint2DBase::BreakForce), kBreakTip));
            fields.push_back(Tip(BaseField<D>("Break Torque", &Joint2DBase::BreakTorque), kBreakTip));
            return fields;
        }

        // A Joint2DBase-derived component: the common joint fields followed by its own.
        template<typename D>
        void Joint2D(const char* displayName, std::vector<FieldHandle> own, bool withConnectedBody = true) {
            std::vector<FieldHandle> fields = Joint2DFields<D>(withConnectedBody);
            Append(fields, std::move(own));
            Component<D>(displayName, ComponentCategory::Physics2D, std::move(fields));
        }

        const std::vector<std::string> kSelection{ "Rigidbody", "Collider" };
        const char* const kEffectorTip = "Acts on bodies touching this entity's colliders marked Used By Effector "
            "(every collider of the entity when none is marked).";
    }

    void RegisterPhysics2DComponents() {
        Component<PhysicsRaycaster2DComponent>("Physics Raycaster 2D", ComponentCategory::Physics2D, {
            MakeField("Enabled", &PhysicsRaycaster2DComponent::Enabled),
        });

        Component<Rigidbody2DComponent>("Rigidbody 2D", ComponentCategory::Physics2D, {
            MakeField("Enabled", &Rigidbody2DComponent::Enabled),
            MakeField("Body Type", &Rigidbody2DComponent::Type),
            MakeField("Fixed Rotation", &Rigidbody2DComponent::FixedRotation),
            MakeField("Mass", &Rigidbody2DComponent::Mass),
            MakeField("Linear Drag", &Rigidbody2DComponent::LinearDrag),
            MakeField("Angular Drag", &Rigidbody2DComponent::AngularDrag),
            MakeField("Gravity Scale", &Rigidbody2DComponent::GravityScale),
            MakeField("Use Gravity", &Rigidbody2DComponent::UseGravity),
        });

        Component<BoxCollider2DComponent>("Box Collider 2D", ComponentCategory::Physics2D, {
            MakeField("Enabled", &BoxCollider2DComponent::Enabled),
            MakeField("Offset", &BoxCollider2DComponent::Offset),
            MakeField("Size", &BoxCollider2DComponent::Size),
            MakeField("Density", &BoxCollider2DComponent::Density),
            MakeField("Friction", &BoxCollider2DComponent::Friction),
            MakeField("Restitution", &BoxCollider2DComponent::Restitution),
            MakeField("Is Trigger", &BoxCollider2DComponent::IsTrigger),
            MakeField("Used By Effector", &BoxCollider2DComponent::UsedByEffector),
            MakeField("Physics Material", &BoxCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &BoxCollider2DComponent::EditMode),
        });

        Component<CircleCollider2DComponent>("Circle Collider 2D", ComponentCategory::Physics2D, {
            MakeField("Enabled", &CircleCollider2DComponent::Enabled),
            MakeField("Offset", &CircleCollider2DComponent::Offset),
            MakeField("Radius", &CircleCollider2DComponent::Radius),
            MakeField("Density", &CircleCollider2DComponent::Density),
            MakeField("Friction", &CircleCollider2DComponent::Friction),
            MakeField("Restitution", &CircleCollider2DComponent::Restitution),
            MakeField("Is Trigger", &CircleCollider2DComponent::IsTrigger),
            MakeField("Used By Effector", &CircleCollider2DComponent::UsedByEffector),
            MakeField("Physics Material", &CircleCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &CircleCollider2DComponent::EditMode),
        });

        Component<CapsuleCollider2DComponent>("Capsule Collider 2D", ComponentCategory::Physics2D, {
            MakeField("Enabled", &CapsuleCollider2DComponent::Enabled),
            MakeField("Offset", &CapsuleCollider2DComponent::Offset),
            MakeField("Radius", &CapsuleCollider2DComponent::Radius),
            MakeField("Height", &CapsuleCollider2DComponent::Height),
            MakeField("Density", &CapsuleCollider2DComponent::Density),
            MakeField("Friction", &CapsuleCollider2DComponent::Friction),
            MakeField("Restitution", &CapsuleCollider2DComponent::Restitution),
            MakeField("Is Trigger", &CapsuleCollider2DComponent::IsTrigger),
            MakeField("Used By Effector", &CapsuleCollider2DComponent::UsedByEffector),
            MakeField("Physics Material", &CapsuleCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &CapsuleCollider2DComponent::EditMode),
        });

        Component<PolygonCollider2DComponent>("Polygon Collider 2D", ComponentCategory::Physics2D, {
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
            MakeField("Used By Effector", &PolygonCollider2DComponent::UsedByEffector),
            MakeField("Physics Material", &PolygonCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &PolygonCollider2DComponent::EditMode),
        });

        Component<EdgeCollider2DComponent>("Edge Collider 2D", ComponentCategory::Physics2D, {
            MakeField("Enabled", &EdgeCollider2DComponent::Enabled),
            MakeField("Offset", &EdgeCollider2DComponent::Offset),
            Ranged(MakeField("Point Count", &EdgeCollider2DComponent::PointCount), 2.0f, 8.0f, "2..8 points of an open polyline."),
            MakeField("Point 0", &EdgeCollider2DComponent::Point0),
            MakeField("Point 1", &EdgeCollider2DComponent::Point1),
            MakeField("Point 2", &EdgeCollider2DComponent::Point2),
            MakeField("Point 3", &EdgeCollider2DComponent::Point3),
            MakeField("Point 4", &EdgeCollider2DComponent::Point4),
            MakeField("Point 5", &EdgeCollider2DComponent::Point5),
            MakeField("Point 6", &EdgeCollider2DComponent::Point6),
            MakeField("Point 7", &EdgeCollider2DComponent::Point7),
            Tip(MakeField("Edge Radius", &EdgeCollider2DComponent::EdgeRadius), "Rounds the edges (thickness / 2)."),
            MakeField("Density", &EdgeCollider2DComponent::Density),
            MakeField("Friction", &EdgeCollider2DComponent::Friction),
            MakeField("Restitution", &EdgeCollider2DComponent::Restitution),
            MakeField("Is Trigger", &EdgeCollider2DComponent::IsTrigger),
            MakeField("Used By Effector", &EdgeCollider2DComponent::UsedByEffector),
            MakeField("Physics Material", &EdgeCollider2DComponent::PhysicsMaterial),
            MakeField("Edit", &EdgeCollider2DComponent::EditMode),
        });

        // ---- Joints -----------------------------------------------------------------------
        Joint2D<DistanceJoint2DComponent>("Distance Joint 2D", {
            Tip(MakeField("Auto Configure Distance", &DistanceJoint2DComponent::AutoConfigureDistance), "Use the anchors' distance at Play start."),
            MakeField("Distance", &DistanceJoint2DComponent::Distance),
            Tip(MakeField("Max Distance Only", &DistanceJoint2DComponent::MaxDistanceOnly), "Rope: only the maximum distance is enforced."),
        });
        Joint2D<HingeJoint2DComponent>("Hinge Joint 2D", {
            Header(MakeField("Use Motor", &HingeJoint2DComponent::UseMotor), "Motor"),
            Tip(MakeField("Motor Speed", &HingeJoint2DComponent::MotorSpeed), "Degrees per second."),
            MakeField("Max Motor Torque", &HingeJoint2DComponent::MaxMotorTorque),
            Header(MakeField("Use Limits", &HingeJoint2DComponent::UseLimits), "Angle Limits"),
            Ranged(MakeField("Lower Angle", &HingeJoint2DComponent::LowerAngle), -359.0f, 359.0f, "Degrees from the start pose."),
            Ranged(MakeField("Upper Angle", &HingeJoint2DComponent::UpperAngle), -359.0f, 359.0f, "Degrees from the start pose."),
        });
        Joint2D<SpringJoint2DComponent>("Spring Joint 2D", {
            MakeField("Auto Configure Distance", &SpringJoint2DComponent::AutoConfigureDistance),
            MakeField("Distance", &SpringJoint2DComponent::Distance),
            Ranged(MakeField("Damping Ratio", &SpringJoint2DComponent::DampingRatio), 0.0f, 1.0f),
            Ranged(MakeField("Frequency", &SpringJoint2DComponent::Frequency), 0.0f, 30.0f, "Oscillations per second (0 = rigid)."),
        });
        Joint2D<FixedJoint2DComponent>("Fixed Joint 2D", {
            Ranged(MakeField("Damping Ratio", &FixedJoint2DComponent::DampingRatio), 0.0f, 1.0f),
            Ranged(MakeField("Frequency", &FixedJoint2DComponent::Frequency), 0.0f, 30.0f, "0 = perfectly rigid weld."),
        });
        Joint2D<SliderJoint2DComponent>("Slider Joint 2D", {
            Tip(MakeField("Auto Configure Angle", &SliderJoint2DComponent::AutoConfigureAngle), "Use the direction to the connected anchor at Play start."),
            Ranged(MakeField("Angle", &SliderJoint2DComponent::Angle), -359.0f, 359.0f, "Degrees, 0 = +X, 90 = up (-Y)."),
            Header(MakeField("Use Motor", &SliderJoint2DComponent::UseMotor), "Motor"),
            MakeField("Motor Speed", &SliderJoint2DComponent::MotorSpeed),
            MakeField("Max Motor Force", &SliderJoint2DComponent::MaxMotorForce),
            Header(MakeField("Use Limits", &SliderJoint2DComponent::UseLimits), "Translation Limits"),
            MakeField("Lower Translation", &SliderJoint2DComponent::LowerTranslation),
            MakeField("Upper Translation", &SliderJoint2DComponent::UpperTranslation),
        });
        Joint2D<WheelJoint2DComponent>("Wheel Joint 2D", {
            Header(Ranged(MakeField("Damping Ratio", &WheelJoint2DComponent::SuspensionDampingRatio), 0.0f, 1.0f), "Suspension"),
            Ranged(MakeField("Frequency", &WheelJoint2DComponent::SuspensionFrequency), 0.0f, 30.0f),
            Ranged(MakeField("Angle", &WheelJoint2DComponent::SuspensionAngle), -359.0f, 359.0f, "Suspension axis in degrees, 90 = up (-Y)."),
            Header(MakeField("Use Motor", &WheelJoint2DComponent::UseMotor), "Motor"),
            Tip(MakeField("Motor Speed", &WheelJoint2DComponent::MotorSpeed), "Degrees per second."),
            MakeField("Max Motor Torque", &WheelJoint2DComponent::MaxMotorTorque),
        });
        Joint2D<FrictionJoint2DComponent>("Friction Joint 2D", {
            MakeField("Max Force", &FrictionJoint2DComponent::MaxForce),
            MakeField("Max Torque", &FrictionJoint2DComponent::MaxTorque),
        });
        Joint2D<RelativeJoint2DComponent>("Relative Joint 2D", {
            MakeField("Max Force", &RelativeJoint2DComponent::MaxForce),
            MakeField("Max Torque", &RelativeJoint2DComponent::MaxTorque),
            Ranged(MakeField("Correction Scale", &RelativeJoint2DComponent::CorrectionScale), 0.0f, 1.0f),
            Tip(MakeField("Auto Configure Offset", &RelativeJoint2DComponent::AutoConfigureOffset), "Keep the relative pose at Play start."),
            MakeField("Linear Offset", &RelativeJoint2DComponent::LinearOffset),
            Tip(MakeField("Angular Offset", &RelativeJoint2DComponent::AngularOffset), "Degrees."),
        });
        Joint2D<TargetJoint2DComponent>("Target Joint 2D", {
            Tip(MakeField("Auto Configure Target", &TargetJoint2DComponent::AutoConfigureTarget), "Target = the anchor's world position at Play start."),
            Tip(MakeField("Target", &TargetJoint2DComponent::Target), "World position the anchor is pulled towards."),
            MakeField("Max Force", &TargetJoint2DComponent::MaxForce),
            Ranged(MakeField("Damping Ratio", &TargetJoint2DComponent::DampingRatio), 0.0f, 1.0f),
            Ranged(MakeField("Frequency", &TargetJoint2DComponent::Frequency), 0.0f, 30.0f),
        }, /*withConnectedBody=*/false);

        // ---- Effectors / forces -------------------------------------------------------------
        Component<AreaEffector2DComponent>("Area Effector 2D", ComponentCategory::Physics2D, {
            Tip(MakeField("Enabled", &AreaEffector2DComponent::Enabled), kEffectorTip),
            MakeField("Use Collider Mask", &AreaEffector2DComponent::UseColliderMask),
            MakeField("Collider Mask", &AreaEffector2DComponent::ColliderMask),
            MakeField("Use Global Angle", &AreaEffector2DComponent::UseGlobalAngle),
            Ranged(MakeField("Force Angle", &AreaEffector2DComponent::ForceAngle), -359.0f, 359.0f, "Degrees, 0 = +X, 90 = up (-Y)."),
            MakeField("Force Magnitude", &AreaEffector2DComponent::ForceMagnitude),
            MakeField("Force Variation", &AreaEffector2DComponent::ForceVariation),
            MakeEnumField("Force Target", &AreaEffector2DComponent::ForceTarget, kSelection),
            Header(MakeField("Drag", &AreaEffector2DComponent::Drag), "Damping"),
            MakeField("Angular Drag", &AreaEffector2DComponent::AngularDrag),
        });

        Component<PointEffector2DComponent>("Point Effector 2D", ComponentCategory::Physics2D, {
            Tip(MakeField("Enabled", &PointEffector2DComponent::Enabled), kEffectorTip),
            MakeField("Use Collider Mask", &PointEffector2DComponent::UseColliderMask),
            MakeField("Collider Mask", &PointEffector2DComponent::ColliderMask),
            Tip(MakeField("Force Magnitude", &PointEffector2DComponent::ForceMagnitude), "Negative attracts, positive repels."),
            MakeField("Force Variation", &PointEffector2DComponent::ForceVariation),
            MakeField("Distance Scale", &PointEffector2DComponent::DistanceScale),
            MakeEnumField("Force Source", &PointEffector2DComponent::ForceSource, kSelection),
            MakeEnumField("Force Target", &PointEffector2DComponent::ForceTarget, kSelection),
            MakeEnumField("Force Mode", &PointEffector2DComponent::ForceMode, { "Constant", "Inverse Linear", "Inverse Squared" }),
            Header(MakeField("Drag", &PointEffector2DComponent::Drag), "Damping"),
            MakeField("Angular Drag", &PointEffector2DComponent::AngularDrag),
        });

        Component<SurfaceEffector2DComponent>("Surface Effector 2D", ComponentCategory::Physics2D, {
            Tip(MakeField("Enabled", &SurfaceEffector2DComponent::Enabled), kEffectorTip),
            MakeField("Use Collider Mask", &SurfaceEffector2DComponent::UseColliderMask),
            MakeField("Collider Mask", &SurfaceEffector2DComponent::ColliderMask),
            Tip(MakeField("Speed", &SurfaceEffector2DComponent::Speed), "Conveyor speed along the surface (world units / second)."),
            MakeField("Speed Variation", &SurfaceEffector2DComponent::SpeedVariation),
            Ranged(MakeField("Force Scale", &SurfaceEffector2DComponent::ForceScale), 0.0f, 1.0f),
            MakeField("Use Contact Force", &SurfaceEffector2DComponent::UseContactForce),
            MakeField("Use Friction", &SurfaceEffector2DComponent::UseFriction),
            MakeField("Use Bounce", &SurfaceEffector2DComponent::UseBounce),
        });

        Component<PlatformEffector2DComponent>("Platform Effector 2D", ComponentCategory::Physics2D, {
            Tip(MakeField("Enabled", &PlatformEffector2DComponent::Enabled), kEffectorTip),
            MakeField("Use Collider Mask", &PlatformEffector2DComponent::UseColliderMask),
            MakeField("Collider Mask", &PlatformEffector2DComponent::ColliderMask),
            Ranged(MakeField("Rotational Offset", &PlatformEffector2DComponent::RotationalOffset), -359.0f, 359.0f),
            Header(MakeField("Use One Way", &PlatformEffector2DComponent::UseOneWay), "One Way"),
            MakeField("Use One Way Grouping", &PlatformEffector2DComponent::UseOneWayGrouping),
            Ranged(MakeField("Surface Arc", &PlatformEffector2DComponent::SurfaceArc), 0.0f, 360.0f, "Degrees around the platform's up that collide."),
            Header(MakeField("Use Side Friction", &PlatformEffector2DComponent::UseSideFriction), "Sides"),
            MakeField("Use Side Bounce", &PlatformEffector2DComponent::UseSideBounce),
            Ranged(MakeField("Side Arc", &PlatformEffector2DComponent::SideArc), 0.0f, 180.0f),
        });

        Component<BuoyancyEffector2DComponent>("Buoyancy Effector 2D", ComponentCategory::Physics2D, {
            Tip(MakeField("Enabled", &BuoyancyEffector2DComponent::Enabled), kEffectorTip),
            MakeField("Use Collider Mask", &BuoyancyEffector2DComponent::UseColliderMask),
            MakeField("Collider Mask", &BuoyancyEffector2DComponent::ColliderMask),
            Tip(MakeField("Surface Level", &BuoyancyEffector2DComponent::SurfaceLevel), "Local offset of the fluid surface, positive = up."),
            Header(MakeField("Density", &BuoyancyEffector2DComponent::Density), "Fluid"),
            MakeField("Linear Drag", &BuoyancyEffector2DComponent::LinearDrag),
            MakeField("Angular Drag", &BuoyancyEffector2DComponent::AngularDrag),
            Header(Ranged(MakeField("Flow Angle", &BuoyancyEffector2DComponent::FlowAngle), -359.0f, 359.0f), "Flow"),
            MakeField("Flow Magnitude", &BuoyancyEffector2DComponent::FlowMagnitude),
            MakeField("Flow Variation", &BuoyancyEffector2DComponent::FlowVariation),
        });

        Component<ConstantForce2DComponent>("Constant Force 2D", ComponentCategory::Physics2D, {
            MakeField("Enabled", &ConstantForce2DComponent::Enabled),
            Tip(MakeField("Force", &ConstantForce2DComponent::Force), "World space, every fixed step."),
            Tip(MakeField("Relative Force", &ConstantForce2DComponent::RelativeForce), "Body space."),
            MakeField("Torque", &ConstantForce2DComponent::Torque),
        });
    }

}
