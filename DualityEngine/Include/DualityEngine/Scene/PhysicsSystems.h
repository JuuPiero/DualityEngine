#pragma once

// Hooks through which the physics systems (Source/Scene/Systems/Physics2DSystem.cpp and
// Physics3DSystem.cpp) drive the components that need more than a body + collider: Edge
// Collider 2D, the 2D joints / effectors / Constant Force 2D (Box2D, Physics2DJoints.cpp) and
// Mesh Collider, 3D joints, Constant Force and Character Controller (Bullet,
// Physics3DJoints.cpp). World handles are opaque pointers (b2World* / btDiscreteDynamicsWorld*)
// so this header stays free of Box2D/Bullet includes, like Scene.h.
//
// Cost when unused: every per-step hook is a handful of empty EnTT views.

#include <vector>

#include <entt.hpp>
#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    class Scene;

    namespace PhysicsSystems {

        // ---------------------------------------------------------------- Box2D (2D)
        // True when the entity has an enabled collider type owned by this module (Edge).
        bool HasEnabledCollider2D(entt::registry& registry, entt::entity entity);
        // Adds this module's collider fixtures (Edge Collider 2D) to an already created body.
        void CreateColliderFixtures2D(Scene& scene, entt::entity entity, void* body);
        // After CreatePhysicsBodies: joints for the newly created bodies (`only` == nullptr =
        // every entity, at Play start).
        void OnBodiesCreated2D(Scene& scene, void* world, const std::vector<entt::entity>* only);
        // Before / after every fixed b2World::Step.
        void PreStep2D(Scene& scene, void* world, float fixedDeltaTime);
        void PostStep2D(Scene& scene, void* world, float fixedDeltaTime);
        // Forwarded from the Box2D contact listener (b2Contact*, const b2Manifold*).
        void PreSolve2D(Scene& scene, void* contact, const void* oldManifold);
        void EndContact2D(Scene& scene, void* contact);
        // Before the entity's body is destroyed mid-Play (Destroy()).
        void ReleaseEntity2D(Scene& scene, void* world, entt::entity entity);
        // Right before the b2World is deleted (OnRuntimeStop): clears every runtime pointer.
        void Shutdown2D(Scene& scene);

        // --------------------------------------------------------------- Bullet (3D)
        bool HasEnabledCollider3D(entt::registry& registry, entt::entity entity);
        bool IsTrigger3D(entt::registry& registry, entt::entity entity);
        // Mesh Collider shape (btCollisionShape*, owning its triangle data) or nullptr. `volume`
        // is a mass estimate (bounding box volume of the scaled mesh).
        void* CreateColliderShape3D(Scene& scene, entt::entity entity, bool dynamicBody,
                                    float& density, float& friction, float& restitution,
                                    bool& isTrigger, float& volume);
        void OnBodiesCreated3D(Scene& scene, void* world, const std::vector<entt::entity>* only);
        void PreStep3D(Scene& scene, void* world, float fixedDeltaTime);
        void PostStep3D(Scene& scene, void* world, float fixedDeltaTime);
        void ReleaseEntity3D(Scene& scene, void* world, entt::entity entity);
        void Shutdown3D(Scene& scene, void* world);

        // Unity CharacterController.Move / SimpleMove (collide-and-slide). Return Unity's
        // CollisionFlags / isGrounded. Only valid during Play; outside Play the motion is applied
        // to the Transform without collision.
        int CharacterMove(Scene& scene, Entity entity, const glm::vec3& motion);
        bool CharacterSimpleMove(Scene& scene, Entity entity, const glm::vec3& speed);

        // Shared by both modules: dispatches a script callback to every enabled script of the
        // entity after the physics step (OnJointBreak / OnControllerColliderHit...).
        struct PendingCallback {
            entt::entity Entity = entt::null;
            entt::entity Other = entt::null;
            int Kind = 0;              // see Physics3DJoints.cpp
            float Value = 0.0f;
            glm::vec3 Point{ 0.0f }, Normal{ 0.0f }, Direction{ 0.0f };
        };

    }

}
