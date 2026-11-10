#pragma once

// Bullet-backed 3D physics components: Rigidbody 3D, the 3D colliders (incl. Mesh Collider),
// Character Controller, joints, Constant Force and the Physics Raycaster 3D. Part of
// Scene/Components.h. Runtime pointers are opaque so this header needs no Bullet include.

#include <cstdint>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Physics/BodyType.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Unity's CollisionFlags (bit mask returned by CharacterController.Move).
    enum CharacterCollisionFlags : int {
        CollisionNone = 0,
        CollisionSides = 1,
        CollisionAbove = 2,
        CollisionBelow = 4,
    };

    // Bullet-backed 3D physics (btDiscreteDynamicsWorld owned by Scene, see
    // Physics3DWorld in Scene.cpp), same shape/lifetime convention as the
    // Box2D components above: RuntimeBody is opaque (actually btRigidBody*)
    // so this header doesn't need to include Bullet, only valid between
    // Scene::OnRuntimeStart and OnRuntimeStop, never serialized.
    struct Rigidbody3DComponent {
        bool Enabled = true;
        BodyType Type = BodyType::Dynamic;
        float Mass = 0.0f;
        float LinearDrag = 0.0f;
        float AngularDrag = 0.05f;
        float GravityScale = 1.0f;
        bool UseGravity = true;
        void* RuntimeBody = nullptr;
        void* RuntimeCollisionShape = nullptr;
    };

    struct BoxCollider3DComponent {
        bool Enabled = true;
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Size{ 0.16f, 0.16f, 0.16f }; // half-extents, in the same world units as Transform
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        // Bullet has no native per-shape sensor concept the way Box2D does -- a trigger body
        // gets btCollisionObject::CF_NO_CONTACT_RESPONSE set at creation instead (contact
        // manifolds still generate, so the manual enter/exit diffing in Scene.cpp still
        // detects it, but the physical push-apart response is suppressed). Same
        // "either side is a trigger -> trigger callback" rule as the 2D colliders.
        bool IsTrigger = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
    };

    struct SphereCollider3DComponent {
        bool Enabled = true;
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        float Radius = 0.16f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
    };

    struct CapsuleCollider3DComponent {
        bool Enabled = true;
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        float Radius = 0.08f;
        float Height = 0.32f;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        bool IsTrigger = false;
        AssetRef PhysicsMaterial;
        bool EditMode = false;
    };

    struct Joint3DBase {
        bool Enabled = true;
        EntityRef ConnectedBody;
        glm::vec3 Anchor{ 0.0f, 0.0f, 0.0f };
        bool AutoConfigureConnectedAnchor = true;
        glm::vec3 ConnectedAnchor{ 0.0f, 0.0f, 0.0f };
        bool EnableCollision = false;
        float BreakForce = 0.0f;  // 0 = Infinity
        float BreakTorque = 0.0f; // 0 = Infinity

        // Runtime (Play only).
        void* RuntimeConstraint = nullptr; // btTypedConstraint* (null for Spring Joint)
        bool RuntimeBroken = false;
        glm::vec3 CurrentForce{ 0.0f };
        glm::vec3 CurrentTorque{ 0.0f };
    };

    struct FixedJointComponent : Joint3DBase {};

    struct HingeJointComponent : Joint3DBase {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f };   // local to this body
        bool UseSpring = false;
        float Spring = 0.0f;
        float Damper = 0.0f;
        float TargetPosition = 0.0f;          // degrees
        bool UseMotor = false;
        float MotorTargetVelocity = 0.0f;     // degrees / second
        float MotorForce = 0.0f;
        bool FreeSpin = false;
        bool UseLimits = false;
        float MinLimit = 0.0f;                // degrees
        float MaxLimit = 0.0f;
        float Bounciness = 0.0f;
        // Runtime.
        float Angle = 0.0f;
        float Velocity = 0.0f;
    };

    // Unity SpringJoint: keeps the anchors between Min and Max Distance (the distance at Play
    // start when both are 0, with Auto Configure) with a damped spring force -- evaluated as a
    // force pair every fixed step, not a Bullet constraint.
    struct SpringJointComponent : Joint3DBase {
        float Spring = 10.0f;
        float Damper = 0.2f;
        float MinDistance = 0.0f;
        float MaxDistance = 0.0f;
        float Tolerance = 0.025f;
        // Runtime.
        float RuntimeRestMin = 0.0f;
        float RuntimeRestMax = 0.0f;
    };

    // Ragdoll-style ball joint with a twist range around Axis and an elliptical swing cone
    // (Bullet cone-twist constraint).
    struct CharacterJointComponent : Joint3DBase {
        glm::vec3 Axis{ 1.0f, 0.0f, 0.0f };
        glm::vec3 SwingAxis{ 0.0f, 1.0f, 0.0f };
        float LowTwistLimit = -20.0f;
        float HighTwistLimit = 70.0f;
        float Swing1Limit = 40.0f;
        float Swing2Limit = 40.0f;
    };

    // Unity MeshCollider. Mesh empty = the entity's Mesh Renderer mesh (or its primitive). A
    // static collider (no Rigidbody 3D, or a Static/Kinematic one) uses the exact triangle mesh;
    // a Dynamic Rigidbody needs Convex (a convex hull is used either way and a warning logged
    // if Convex is off). Transform scale is applied, like a rendered mesh.
    struct MeshColliderComponent {
        bool Enabled = true;
        AssetRef Mesh;
        bool Convex = false;
        bool IsTrigger = false;
        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        AssetRef PhysicsMaterial;
    };

    // Unity CharacterController: a kinematic capsule moved only by Move/SimpleMove (collide and
    // slide with Bullet convex sweeps, Slope Limit, Step Offset). Height is the TOTAL height
    // including both caps, Center is local. Never pushed by rigidbodies; collides with every
    // non-trigger collider in the world.
    struct CharacterControllerComponent {
        bool Enabled = true;
        float SlopeLimit = 45.0f;
        float StepOffset = 0.3f;
        float SkinWidth = 0.08f;
        float MinMoveDistance = 0.001f;
        glm::vec3 Center{ 0.0f, 0.0f, 0.0f };
        float Radius = 0.5f;
        float Height = 2.0f;

        // Runtime.
        bool IsGrounded = false;
        int CollisionFlags = CollisionNone;
        glm::vec3 Velocity{ 0.0f };
        float SimpleMoveFallSpeed = 0.0f;
        glm::vec3 RuntimeLastPosition{ 0.0f };
        float RuntimeVelocityTime = 0.0f;
        void* RuntimeObject = nullptr; // btCollisionObject*
        void* RuntimeShape = nullptr;  // btCapsuleShape*
    };

    struct ConstantForceComponent {
        bool Enabled = true;
        glm::vec3 Force{ 0.0f };
        glm::vec3 RelativeForce{ 0.0f };
        glm::vec3 Torque{ 0.0f };
        glm::vec3 RelativeTorque{ 0.0f };
    };

    // Argument of Behaviour::OnControllerColliderHit (Unity's ControllerColliderHit).
    struct ControllerColliderHit {
        Entity Controller;              // the entity with the Character Controller
        Entity Other;                   // the collider that was hit
        glm::vec3 Point{ 0.0f };        // world contact point
        glm::vec3 Normal{ 0.0f };       // world surface normal
        glm::vec3 MoveDirection{ 0.0f };
        float MoveLength = 0.0f;
    };

    // Unity PhysicsRaycaster equivalent -- pair with CameraComponent on the same entity.
    // UpdatePhysicsRaycasterInteractions casts a 3D ray from this camera through the pointer
    // and dispatches IPointer* callbacks on Bullet collider hits. Auto-added with Camera when
    // Projection is Perspective (see Reflection.cpp).
    struct PhysicsRaycaster3DComponent {
        bool Enabled = true;
        float MaxDistance = 2000.0f;

        // Runtime-only (not reflected/serialized).
        entt::entity HoveredEntity = entt::null;
        entt::entity PressedEntity = entt::null;
        Screen PressedScreen = Screen::Top;
        glm::vec2 PressedPosition{ 0.0f };
    };

}
