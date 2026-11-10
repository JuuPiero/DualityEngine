#pragma once

// Box2D-backed 2D physics components: Rigidbody 2D, the 2D colliders, joints, effectors,
// Constant Force 2D and the Physics Raycaster 2D. Part of Scene/Components.h. Runtime pointers
// (RuntimeBody/RuntimeFixture/RuntimeJoint) are opaque so this header needs no Box2D include.

#include <cstdint>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Physics/BodyType.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    enum class EffectorSelection2D { Rigidbody, Collider };
    enum class EffectorForceMode2D { Constant, InverseLinear, InverseSquared };

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
        glm::vec2 Size{ 0.16f, 0.16f }; // half-extents, in the same world units as Transform
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        // Box2D's own native sensor flag (fixtureDef.isSensor) -- a trigger still generates
        // Begin/EndContact (so OnTriggerEnter/Exit fire) but never physically resolves the
        // overlap. Unity's own rule: a contact fires as a TRIGGER callback if EITHER side is
        // a trigger, and as a COLLISION callback only when NEITHER side is.
        bool IsTrigger = false;
        bool UsedByEffector = false; // this collider is the shape of an Effector2D on the same entity
        AssetRef PhysicsMaterial;
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

    struct CircleCollider2DComponent {
        bool Enabled = true;
        glm::vec2 Offset{ 0.0f, 0.0f };
        float Radius = 0.16f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false; // see BoxCollider2DComponent::IsTrigger
        bool UsedByEffector = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false; // see BoxCollider2DComponent::EditMode
        void* RuntimeFixture = nullptr;
    };

    // Capsule (2D) -- approximated as a fixed 8-vertex polygon for Box2D v2.4.
    struct CapsuleCollider2DComponent {
        bool Enabled = true;
        glm::vec2 Offset{ 0.0f, 0.0f };
        float Radius = 0.08f;
        float Height = 0.32f; // total height including hemispheres
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        bool UsedByEffector = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

    // Convex polygon (max 8 vertices) for Box2D.
    struct PolygonCollider2DComponent {
        bool Enabled = true;
        glm::vec2 Offset{ 0.0f, 0.0f };
        int VertexCount = 4;
        glm::vec2 Vertex0{ -0.08f, -0.08f };
        glm::vec2 Vertex1{ 0.08f, -0.08f };
        glm::vec2 Vertex2{ 0.08f, 0.08f };
        glm::vec2 Vertex3{ -0.08f, 0.08f };
        glm::vec2 Vertex4{}, Vertex5{}, Vertex6{}, Vertex7{};
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        bool UsedByEffector = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

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

    // Unity EdgeCollider2D: an open polyline of up to 8 points (a Box2D chain shape -- one
    // fixture whose edges have no inside, so bodies collide from either side). Points are
    // local to the entity (plus Offset), the same convention as PolygonCollider2D.
    inline constexpr int MaxEdgeCollider2DPoints = 8;
    struct EdgeCollider2DComponent {
        bool Enabled = true;
        glm::vec2 Offset{ 0.0f, 0.0f };
        int PointCount = 2;
        glm::vec2 Point0{ -0.16f, 0.0f };
        glm::vec2 Point1{ 0.16f, 0.0f };
        glm::vec2 Point2{}, Point3{}, Point4{}, Point5{}, Point6{}, Point7{};
        float EdgeRadius = 0.0f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        bool UsedByEffector = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
        void* RuntimeFixture = nullptr;
    };

    inline glm::vec2& GetEdgePoint2D(EdgeCollider2DComponent& edge, int index) {
        switch (index) {
            case 0: return edge.Point0;
            case 1: return edge.Point1;
            case 2: return edge.Point2;
            case 3: return edge.Point3;
            case 4: return edge.Point4;
            case 5: return edge.Point5;
            case 6: return edge.Point6;
            default: return edge.Point7;
        }
    }
    inline const glm::vec2& GetEdgePoint2D(const EdgeCollider2DComponent& edge, int index) {
        return GetEdgePoint2D(const_cast<EdgeCollider2DComponent&>(edge), index);
    }

    // Fields every Unity Joint2D has. The joint lives on the entity's own Rigidbody2D (body A);
    // Connected Body is another entity with a Rigidbody2D, or empty to pin to the world.
    // Anchor is local to this body, Connected Anchor local to the connected body (or a world
    // point when there is none). With Auto Configure Connected Anchor the connected anchor is
    // computed at Play start so the joint starts satisfied, exactly like Unity.
    struct Joint2DBase {
        bool Enabled = true;
        EntityRef ConnectedBody;
        bool EnableCollision = false;
        bool AutoConfigureConnectedAnchor = true;
        glm::vec2 Anchor{ 0.0f, 0.0f };
        glm::vec2 ConnectedAnchor{ 0.0f, 0.0f };
        float BreakForce = 0.0f;  // 0 = Infinity
        float BreakTorque = 0.0f; // 0 = Infinity

        // Runtime (Play only).
        void* RuntimeJoint = nullptr;   // b2Joint*
        bool RuntimeBroken = false;
        glm::vec2 ReactionForce{ 0.0f }; // last fixed step, world units
        float ReactionTorque = 0.0f;
    };

    struct HingeJoint2DComponent : Joint2DBase {
        bool UseMotor = false;
        float MotorSpeed = 0.0f;        // degrees / second
        float MaxMotorTorque = 10000.0f;
        bool UseLimits = false;
        float LowerAngle = 0.0f;        // degrees, relative to the start pose
        float UpperAngle = 359.0f;
        // Runtime.
        float JointAngle = 0.0f;
        float JointSpeed = 0.0f;
    };

    struct DistanceJoint2DComponent : Joint2DBase {
        bool AutoConfigureDistance = true;
        float Distance = 1.0f;
        bool MaxDistanceOnly = false;
    };

    struct SpringJoint2DComponent : Joint2DBase {
        bool AutoConfigureDistance = true;
        float Distance = 1.0f;
        float DampingRatio = 0.0f;
        float Frequency = 1.0f;
    };

    struct FixedJoint2DComponent : Joint2DBase {
        float DampingRatio = 0.0f;
        float Frequency = 0.0f; // 0 = perfectly rigid
    };

    struct SliderJoint2DComponent : Joint2DBase {
        bool AutoConfigureAngle = true;
        float Angle = 0.0f;              // degrees, 90 = screen up (-Y)
        bool UseMotor = false;
        float MotorSpeed = 0.0f;         // world units / second
        float MaxMotorForce = 10000.0f;
        bool UseLimits = false;
        float LowerTranslation = 0.0f;
        float UpperTranslation = 0.0f;
        // Runtime.
        float JointTranslation = 0.0f;
        float JointSpeed = 0.0f;
    };

    struct WheelJoint2DComponent : Joint2DBase {
        float SuspensionDampingRatio = 0.7f;
        float SuspensionFrequency = 2.0f;
        float SuspensionAngle = 90.0f;   // degrees, 90 = screen up (-Y)
        bool UseMotor = false;
        float MotorSpeed = 0.0f;         // degrees / second
        float MaxMotorTorque = 10000.0f;
        // Runtime.
        float JointSpeed = 0.0f;
    };

    struct FrictionJoint2DComponent : Joint2DBase {
        float MaxForce = 1.0f;
        float MaxTorque = 1.0f;
    };

    struct RelativeJoint2DComponent : Joint2DBase {
        float MaxForce = 10000.0f;
        float MaxTorque = 10000.0f;
        float CorrectionScale = 0.3f;
        bool AutoConfigureOffset = true;
        glm::vec2 LinearOffset{ 0.0f, 0.0f };
        float AngularOffset = 0.0f; // degrees
    };

    // Pulls Anchor (local to this body) towards Target (world point) with a spring. Connected
    // Body is ignored (Unity's TargetJoint2D has none).
    struct TargetJoint2DComponent : Joint2DBase {
        bool AutoConfigureTarget = true;
        glm::vec2 Target{ 0.0f, 0.0f };
        float MaxForce = 10000.0f;
        float DampingRatio = 1.0f;
        float Frequency = 5.0f;
    };

    // Effectors act on bodies touching the entity's colliders that have Used By Effector set
    // (if none of its colliders has it set, every collider of the entity is used -- a lenient
    // deviation from Unity documented in the Inspector tooltip). Area/Point/Buoyancy normally
    // use a trigger collider; Surface/Platform a solid one.
    struct AreaEffector2DComponent {
        bool Enabled = true;
        bool UseColliderMask = false;
        uint32_t ColliderMask = 0xFFFFFFFFu;
        bool UseGlobalAngle = false;
        float ForceAngle = 0.0f;         // degrees, 90 = screen up (-Y)
        float ForceMagnitude = 0.0f;
        float ForceVariation = 0.0f;
        EffectorSelection2D ForceTarget = EffectorSelection2D::Rigidbody;
        float Drag = 0.0f;
        float AngularDrag = 0.0f;
    };

    struct PointEffector2DComponent {
        bool Enabled = true;
        bool UseColliderMask = false;
        uint32_t ColliderMask = 0xFFFFFFFFu;
        float ForceMagnitude = 0.0f;     // negative attracts, positive repels (Unity)
        float ForceVariation = 0.0f;
        float DistanceScale = 1.0f;
        EffectorSelection2D ForceSource = EffectorSelection2D::Rigidbody;
        EffectorSelection2D ForceTarget = EffectorSelection2D::Rigidbody;
        EffectorForceMode2D ForceMode = EffectorForceMode2D::InverseSquared;
        float Drag = 0.0f;
        float AngularDrag = 0.0f;
    };

    struct SurfaceEffector2DComponent {
        bool Enabled = true;
        bool UseColliderMask = false;
        uint32_t ColliderMask = 0xFFFFFFFFu;
        float Speed = 0.0f;              // along the surface tangent, world units / second
        float SpeedVariation = 0.0f;
        float ForceScale = 0.1f;
        bool UseContactForce = false;
        bool UseFriction = true;
        bool UseBounce = true;
    };

    // One-way platform: contacts whose normal is outside Surface Arc around the platform's up
    // (-Y rotated by the entity's rotation and Rotational Offset) are disabled until the bodies
    // separate, so things jump through from below and land on top.
    struct PlatformEffector2DComponent {
        bool Enabled = true;
        bool UseColliderMask = false;
        uint32_t ColliderMask = 0xFFFFFFFFu;
        float RotationalOffset = 0.0f;   // degrees
        bool UseOneWay = true;
        bool UseOneWayGrouping = false;
        float SurfaceArc = 180.0f;       // degrees
        bool UseSideFriction = false;
        bool UseSideBounce = false;
        float SideArc = 1.0f;            // degrees
    };

    // Fluid volume: everything below Surface Level (local Y offset, measured towards screen up)
    // inside the effector's collider gets buoyancy (Density x submerged area x gravity), linear /
    // angular drag and an optional flow force.
    struct BuoyancyEffector2DComponent {
        bool Enabled = true;
        bool UseColliderMask = false;
        uint32_t ColliderMask = 0xFFFFFFFFu;
        float SurfaceLevel = 0.0f;
        float Density = 2.0f;
        float LinearDrag = 1.0f;
        float AngularDrag = 1.0f;
        float FlowAngle = 0.0f;          // degrees, 90 = screen up (-Y)
        float FlowMagnitude = 0.0f;
        float FlowVariation = 0.0f;
    };

    struct ConstantForce2DComponent {
        bool Enabled = true;
        glm::vec2 Force{ 0.0f, 0.0f };          // world space
        glm::vec2 RelativeForce{ 0.0f, 0.0f };  // body space
        float Torque = 0.0f;
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

}
