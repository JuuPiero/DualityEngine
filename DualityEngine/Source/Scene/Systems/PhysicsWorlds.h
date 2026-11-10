#pragma once

// Internal to DualityEngine/Source/Scene: the Box2D / Bullet world bundles a running Scene owns
// (Scene::m_PhysicsWorld / m_PhysicsWorld3D are opaque void* in Scene.h so that public header
// needs neither library) and the small helpers shared by Scene.cpp and the physics systems
// (Physics2DSystem.cpp, Physics3DSystem.cpp, Physics2DJoints.cpp, Physics3DJoints.cpp).

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

#include <box2d/box2d.h>
#include <btBulletDynamicsCommon.h>
#include <entt.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/PhysicsMaterialLoader.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/PhysicsSystems.h"

namespace Duality {

    // Physics operates directly in public world units. PPU is deliberately absent here:
    // it is a 2D renderer scale only (SceneRenderer.cpp). +Y remains down for 2D.
    inline constexpr int32 VelocityIterations = 6;
    inline constexpr int32 PositionIterations = 2;

    inline void ApplyPhysicsMaterial(const AssetRef& ref, float& friction, float& restitution, float& density) {
        if (ref.Guid.empty())
            return;
        std::string path = AssetDatabase::ResolvePath(ref.Guid);
        if (path.empty())
            return;
        PhysicsMaterial mat = PhysicsMaterialLoader::Load(path);
        friction = mat.Friction;
        restitution = mat.Restitution;
        density = mat.Density;
    }

    inline b2PolygonShape MakeCapsulePolygon2D(float radius, float height) {
        b2PolygonShape shape;
        float halfH = std::max(0.0f, height * 0.5f - radius);
        b2Vec2 verts[8];
        verts[0] = { -radius, -halfH };
        verts[1] = { radius, -halfH };
        verts[2] = { radius, 0.0f };
        verts[3] = { radius, halfH };
        verts[4] = { -radius, halfH };
        verts[5] = { -radius, 0.0f };
        verts[6] = { -radius * 0.7f, -halfH - radius * 0.7f };
        verts[7] = { radius * 0.7f, -halfH - radius * 0.7f };
        shape.Set(verts, 8);
        return shape;
    }

    // One event per touching-pair transition this frame, queued by whichever physics
    // system detected it (Box2D's listener callback, or Bullet's manual manifold diff --
    // see below) and drained by Scene::OnRuntimeUpdate right after both physics steps,
    // before the Behaviour::OnUpdate loop. IsBegin true = Enter, false = Exit.
    struct PhysicsContactEvent {
        entt::entity A, B;
        bool IsTrigger;
        bool IsBegin;
    };

    // Box2D has a real listener API (unlike Bullet, see the manual manifold diff in
    // OnRuntimeUpdate below) -- BeginContact/EndContact fire synchronously from inside
    // b2World::Step, so `Events` just needs to point at OnRuntimeUpdate's own local
    // event list for the duration of that one Step call (repointed every frame, not a
    // persistent buffer -- see Physics2DWorld below).
    class Box2DContactListener : public b2ContactListener {
    public:
        std::vector<PhysicsContactEvent>* Events = nullptr;
        // Effectors (one-way platforms, conveyor surfaces) need per-contact PreSolve control.
        Scene* Owner = nullptr;

        void BeginContact(b2Contact* contact) override { Record(contact, true); }
        void EndContact(b2Contact* contact) override {
            if (Owner)
                PhysicsSystems::EndContact2D(*Owner, contact);
            Record(contact, false);
        }
        void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override {
            if (Owner)
                PhysicsSystems::PreSolve2D(*Owner, contact, oldManifold);
        }

    private:
        void Record(b2Contact* contact, bool begin) {
            b2Fixture* fixtureA = contact->GetFixtureA();
            b2Fixture* fixtureB = contact->GetFixtureB();
            // uintptr_t -> uint32_t -> entt::entity, matching the same narrowing already
            // used everywhere else this codebase crosses an opaque-handle boundary (e.g.
            // EngineServices_FindEntityInScreen's own unsigned-int handle convention).
            entt::entity a = static_cast<entt::entity>(static_cast<uint32_t>(fixtureA->GetBody()->GetUserData().pointer));
            entt::entity b = static_cast<entt::entity>(static_cast<uint32_t>(fixtureB->GetBody()->GetUserData().pointer));
            bool isTrigger = fixtureA->IsSensor() || fixtureB->IsSensor(); // Unity's own "either side" rule
            if (Events)
                Events->push_back({ a, b, isTrigger, begin });
        }
    };

    // Box2D's own b2World owns/frees every body and fixture it creates (see
    // OnRuntimeStop), but the *listener* is caller-owned and must outlive it -- bundled
    // together the same way Physics3DWorld bundles Bullet's config/dispatcher/etc., so
    // Scene::m_PhysicsWorld can stay one opaque void*.
    struct Physics2DWorld {
        b2World* World = nullptr;
        Box2DContactListener* Listener = nullptr;

        // Collider-only entities are represented by internal static Box2D bodies.  They
        // deliberately do not get a public Rigidbody2DComponent: a Collider in Unity is
        // already queryable/collidable on its own, while Rigidbody opts into simulation.
        struct StaticColliderBody {
            entt::entity Entity = entt::null;
            b2Body* Body = nullptr;
        };
        std::vector<StaticColliderBody> StaticColliderBodies;
    };
    inline Physics2DWorld* PhysicsWorld2D(void* handle) { return static_cast<Physics2DWorld*>(handle); }

    // Bullet-backed 3D physics. Same PhysicsUnits conversion and +Y-down
    // gravity as the 2D world above. Unlike Box2D, Bullet has no single
    // "world" object -- it's a small bundle of a collision configuration/dispatcher/broadphase/solver that
    // all must outlive the btDiscreteDynamicsWorld built from them and be
    // torn down in reverse afterward. Heap-allocated as one unit so
    // Scene::m_PhysicsWorld3D can stay an opaque void* like m_PhysicsWorld.
    struct Physics3DWorld {
        btDefaultCollisionConfiguration* CollisionConfig = nullptr;
        btCollisionDispatcher* Dispatcher = nullptr;
        btBroadphaseInterface* Broadphase = nullptr;
        btSequentialImpulseConstraintSolver* Solver = nullptr;
        btDiscreteDynamicsWorld* World = nullptr;

        // Bullet has no BeginContact/EndContact-style listener (unlike Box2D above) --
        // just "who is touching right now" via the dispatcher's contact manifolds each
        // frame. Enter/exit is detected by diffing that against last frame's set here,
        // in Scene::OnRuntimeUpdate. Canonically ordered (lower entt::entity value first)
        // so a pair only ever appears one way regardless of manifold body0/body1 order.
        std::set<std::pair<entt::entity, entt::entity>> TouchingPairs;

        // Same ownership model as the 2D counterpart above.  Bullet does not own rigid
        // bodies or their shapes, so OnRuntimeStop explicitly releases these entries.
        struct StaticColliderBody {
            entt::entity Entity = entt::null;
            btRigidBody* Body = nullptr;
            btCollisionShape* CollisionShape = nullptr;
        };
        std::vector<StaticColliderBody> StaticColliderBodies;
    };
    inline Physics3DWorld* PhysicsWorld3D(void* handle) { return static_cast<Physics3DWorld*>(handle); }

    // A BoxCollider3D/SphereCollider3D's Offset (mirroring Box2D fixtures'
    // own Offset field) has no direct Bullet equivalent -- a plain
    // btBoxShape/btSphereShape is always centered on the body origin, so a
    // non-zero offset needs a one-child btCompoundShape wrapping the real
    // shape at a local transform instead. Cleanup in Scene::OnRuntimeStop
    // just checks btCollisionShape::isCompound() on Rigidbody3DComponent's
    // own RuntimeCollisionShape and, if so, also deletes its one child --
    // no separate bookkeeping struct needed since btCompoundShape itself
    // already tracks its children.

    // TransformComponent::Rotation is Euler degrees composed the same way
    // OpenGLRenderer3D/Citro3DRenderer build a mesh's model matrix
    // (ComposeWorldMtx: M = T * Rz(z) * Ry(y) * Rx(x)) -- these two
    // functions are the exact quaternion equivalent of that composition (and
    // its inverse), so a Rigidbody3D-driven mesh rotates identically to how
    // it's rendered, and physics results read back into Rotation round-trip
    // through the same convention 2D's glm::degrees(body->GetAngle()) does.
    inline btQuaternion EulerDegreesToBtQuaternion(const glm::vec3& rotationDegrees) {
        btQuaternion qx(btVector3(1.0f, 0.0f, 0.0f), glm::radians(rotationDegrees.x));
        btQuaternion qy(btVector3(0.0f, 1.0f, 0.0f), glm::radians(rotationDegrees.y));
        btQuaternion qz(btVector3(0.0f, 0.0f, 1.0f), glm::radians(rotationDegrees.z));
        return qz * qy * qx;
    }

    // Inverse of EulerDegreesToBtQuaternion -- classic ZYX Tait-Bryan
    // extraction from a rotation matrix R = Rz*Ry*Rx, re-derived by hand
    // from the same Rx/Ry/Rz definitions glm::rotate uses (not taken from
    // GLM's own extractEulerAngleXYZ, which negates its input angles
    // internally and would need its own careful sign-mapping to verify).
    // Degenerates at pitch (y) near +-90 deg (gimbal lock) -- an accepted,
    // standard limitation of any Euler-angle representation, same as the 2D
    // path's own glm::degrees(body->GetAngle()) sync-back.
    inline glm::vec3 BtQuaternionToEulerDegrees(const btQuaternion& q) {
        glm::mat3 r = glm::mat3_cast(glm::quat(q.w(), q.x(), q.y(), q.z()));
        float y = std::asin(std::clamp(-r[0][2], -1.0f, 1.0f));
        float x = std::atan2(r[1][2], r[2][2]);
        float z = std::atan2(r[0][1], r[0][0]);
        return glm::degrees(glm::vec3(x, y, z));
    }

    // Implemented by Physics2DSystem.cpp / Physics3DSystem.cpp.
    bool HasEnabledCollider2D(entt::registry& registry, entt::entity entity);
    bool HasEnabledCollider3D(entt::registry& registry, entt::entity entity);
    void DeleteCollisionShape3D(btCollisionShape* shape);

}
