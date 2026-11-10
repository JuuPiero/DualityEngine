// Bullet side of a running Scene: world creation, rigid bodies + collider shapes, the fixed step
// with its Transform sync and contact diffing, raycasts and teardown. Joints, the Mesh Collider
// shape and the Character Controller live in Physics3DJoints.cpp (PhysicsSystems.h).
#include "PhysicsWorlds.h"

#include <glm/gtc/constants.hpp>

namespace Duality {

bool HasEnabledCollider3D(entt::registry& registry, entt::entity entity) {
        return (registry.all_of<BoxCollider3DComponent>(entity) && registry.get<BoxCollider3DComponent>(entity).Enabled)
            || (registry.all_of<SphereCollider3DComponent>(entity) && registry.get<SphereCollider3DComponent>(entity).Enabled)
            || (registry.all_of<CapsuleCollider3DComponent>(entity) && registry.get<CapsuleCollider3DComponent>(entity).Enabled)
            || PhysicsSystems::HasEnabledCollider3D(registry, entity);
    }

void DeleteCollisionShape3D(btCollisionShape* shape) {
        if (!shape)
            return;
        if (shape->isCompound()) {
            btCompoundShape* compound = static_cast<btCompoundShape*>(shape);
            for (int i = 0; i < compound->getNumChildShapes(); i++)
                delete compound->getChildShape(i);
        }
        // A static Mesh Collider's btBvhTriangleMeshShape does not own its triangle data
        // (Physics3DJoints.cpp allocates it per shape) -- free it with the shape.
        if (shape->getShapeType() == TRIANGLE_MESH_SHAPE_PROXYTYPE) {
            btStridingMeshInterface* triangles = static_cast<btBvhTriangleMeshShape*>(shape)->getMeshInterface();
            delete shape;
            delete triangles;
            return;
        }
        delete shape;
    }

    namespace {
        // The collision shape of an entity's first enabled 3D collider (Box, Sphere, Capsule,
        // else Mesh Collider) with its material values -- the single place collider shapes are
        // built, for Rigidbody 3D bodies and collider-only static bodies alike.
        struct ColliderShape3D {
            btCollisionShape* Shape = nullptr; // already wrapped in a compound when Offset != 0
            float Density = 1.0f;
            float Friction = 0.5f;
            float Restitution = 0.0f;
            float Volume = 0.0f; // 0 = no collider
            bool IsTrigger = false;
        };

        // A BoxCollider3D/SphereCollider3D's Offset has no direct Bullet equivalent -- a plain
        // btBoxShape/btSphereShape is always centered on the body origin, so a non-zero offset
        // needs a one-child btCompoundShape (DeleteCollisionShape3D frees its child too).
        btCollisionShape* WrapWithOffset(btCollisionShape* baseShape, const glm::vec3& offset) {
            if (!baseShape || (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f))
                return baseShape;
            btCompoundShape* compound = new btCompoundShape();
            btTransform localTransform;
            localTransform.setIdentity();
            localTransform.setOrigin(btVector3(offset.x, offset.y, offset.z));
            compound->addChildShape(localTransform, baseShape);
            return compound;
        }

        ColliderShape3D BuildColliderShape3D(Scene& scene, entt::entity entity, bool dynamicBody) {
            entt::registry& registry = scene.Registry();
            ColliderShape3D result;
            btCollisionShape* baseShape = nullptr;
            glm::vec3 offset{ 0.0f };
            if (registry.all_of<BoxCollider3DComponent>(entity) && registry.get<BoxCollider3DComponent>(entity).Enabled) {
                auto& box = registry.get<BoxCollider3DComponent>(entity);
                ApplyPhysicsMaterial(box.PhysicsMaterial, box.Friction, box.Restitution, box.Density);
                baseShape = new btBoxShape(btVector3(PhysicsUnits::ToPhysics(box.Size.x), PhysicsUnits::ToPhysics(box.Size.y), PhysicsUnits::ToPhysics(box.Size.z)));
                offset = box.Offset;
                result.Density = box.Density; result.Friction = box.Friction; result.Restitution = box.Restitution;
                result.Volume = (2.0f * box.Size.x) * (2.0f * box.Size.y) * (2.0f * box.Size.z);
                result.IsTrigger = box.IsTrigger;
            } else if (registry.all_of<SphereCollider3DComponent>(entity) && registry.get<SphereCollider3DComponent>(entity).Enabled) {
                auto& sphere = registry.get<SphereCollider3DComponent>(entity);
                ApplyPhysicsMaterial(sphere.PhysicsMaterial, sphere.Friction, sphere.Restitution, sphere.Density);
                baseShape = new btSphereShape(PhysicsUnits::ToPhysics(sphere.Radius));
                offset = sphere.Offset;
                result.Density = sphere.Density; result.Friction = sphere.Friction; result.Restitution = sphere.Restitution;
                result.Volume = (4.0f / 3.0f) * glm::pi<float>() * sphere.Radius * sphere.Radius * sphere.Radius;
                result.IsTrigger = sphere.IsTrigger;
            } else if (registry.all_of<CapsuleCollider3DComponent>(entity) && registry.get<CapsuleCollider3DComponent>(entity).Enabled) {
                auto& cap = registry.get<CapsuleCollider3DComponent>(entity);
                ApplyPhysicsMaterial(cap.PhysicsMaterial, cap.Friction, cap.Restitution, cap.Density);
                baseShape = new btCapsuleShape(PhysicsUnits::ToPhysics(cap.Radius), PhysicsUnits::ToPhysics(cap.Height));
                offset = cap.Offset;
                result.Density = cap.Density; result.Friction = cap.Friction; result.Restitution = cap.Restitution;
                result.Volume = glm::pi<float>() * cap.Radius * cap.Radius * cap.Height;
                result.IsTrigger = cap.IsTrigger;
            } else if (void* meshShape = PhysicsSystems::CreateColliderShape3D(scene, entity, dynamicBody,
                           result.Density, result.Friction, result.Restitution, result.IsTrigger, result.Volume)) {
                baseShape = static_cast<btCollisionShape*>(meshShape); // Mesh Collider: final shape, no offset
            }
            result.Shape = WrapWithOffset(baseShape, offset);
            return result;
        }

        // b2World::RayCast (classic v2.4 API) reports every fixture along the ray one at a
        // time via ReportFixture, letting the callback itself decide how to narrow down to
        // "closest" -- returning `fraction` clips the ray to that point for all SUBSEQUENT
        // reports, which is exactly the "closest hit so far" behavior Raycast2D wants (Unity's
        // own Physics2D.Raycast semantics), without needing to track/compare fractions by hand.
        class ClosestRayCastCallback2D : public b2RayCastCallback {
        public:
            bool Hit = false;
            b2Fixture* Fixture = nullptr;
            b2Vec2 Point{}, Normal{};

            float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override {
                Hit = true;
                Fixture = fixture;
                Point = point;
                Normal = normal;
                return fraction;
            }
        };
    }

    void Scene::CreatePhysicsWorld3D() {
        Physics3DWorld* world3D = new Physics3DWorld();
        world3D->CollisionConfig = new btDefaultCollisionConfiguration();
        world3D->Dispatcher = new btCollisionDispatcher(world3D->CollisionConfig);
        world3D->Broadphase = new btDbvtBroadphase();
        world3D->Solver = new btSequentialImpulseConstraintSolver();
        world3D->World = new btDiscreteDynamicsWorld(world3D->Dispatcher, world3D->Broadphase, world3D->Solver, world3D->CollisionConfig);
        world3D->World->setGravity(btVector3(0.0f, PhysicsUnits::PhysicsGravity(), 0.0f));
        m_PhysicsWorld3D = world3D;
    }

    RaycastHit3D Scene::Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
        RaycastHit3D result;
        if (!m_PhysicsWorld3D)
            return result; // not Playing -- no btDiscreteDynamicsWorld to query yet

        glm::vec3 dir = glm::normalize(direction);
        glm::vec3 fromWorld = origin;
        glm::vec3 to = fromWorld + dir * PhysicsUnits::ToPhysics(maxDistance);
        btVector3 from(fromWorld.x, fromWorld.y, fromWorld.z);
        btVector3 toBt(to.x, to.y, to.z);

        btCollisionWorld::ClosestRayResultCallback callback(from, toBt);
        PhysicsWorld3D(m_PhysicsWorld3D)->World->rayTest(from, toBt, callback);
        if (!callback.hasHit())
            return result;

        // Same uintptr_t/void* -> uint32_t -> entt::entity narrowing as the Bullet manifold
        // diff in OnRuntimeUpdate below -- same userPointer convention, just read here instead
        // of at contact time.
        entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(callback.m_collisionObject->getUserPointer())));
        if (!m_Registry.valid(handle))
            return result;

        result.HitEntity = Entity(handle, this);
        result.Point = {
            PhysicsUnits::ToWorld(callback.m_hitPointWorld.x()),
            PhysicsUnits::ToWorld(callback.m_hitPointWorld.y()),
            PhysicsUnits::ToWorld(callback.m_hitPointWorld.z())
        };
        result.Normal = { callback.m_hitNormalWorld.x(), callback.m_hitNormalWorld.y(), callback.m_hitNormalWorld.z() };
        result.Distance = PhysicsUnits::ToWorld(glm::distance(fromWorld, glm::vec3(callback.m_hitPointWorld.x(), callback.m_hitPointWorld.y(), callback.m_hitPointWorld.z())));
        return result;
    }

    // Creates the Bullet bodies for every eligible entity (`only` == nullptr) or only the listed
    // ones; entities that already have a body are skipped. See Scene::CreatePhysicsBodies.
    void Scene::CreatePhysicsBodies3D(const std::vector<entt::entity>* only) {
        if (!m_PhysicsWorld3D)
            return;
        auto selected = [only](entt::entity handle) {
            return !only || std::find(only->begin(), only->end(), handle) != only->end();
        };
        Physics3DWorld* world3D = PhysicsWorld3D(m_PhysicsWorld3D);

        auto body3DView = m_Registry.view<Rigidbody3DComponent, TransformComponent>();
        for (auto handle : body3DView) {
            auto& rb = body3DView.get<Rigidbody3DComponent>(handle);
            if (!selected(handle) || rb.RuntimeBody)
                continue;

            // Same "no body at all if not active at Play start" rule as the 2D loop above.
            if (!IsEffectivelyActive(Entity(handle, this)))
                continue;
            if (!rb.Enabled)
                continue;

            // Same identity-parent-only limitation as the 2D loop above.
            TransformComponent worldTransform = GetWorldTransform(Entity(handle, this));

            ColliderShape3D collider = BuildColliderShape3D(*this, handle, rb.Type == BodyType::Dynamic);
            // A Rigidbody3D with no collider still gets a real body (matching the 2D loop's own
            // zero-fixture case) -- btEmptyShape is Bullet's supported "no collision volume" shape.
            btCollisionShape* attachedShape = collider.Shape ? collider.Shape : new btEmptyShape();
            const float density = collider.Density, friction = collider.Friction, restitution = collider.Restitution;
            const float volume = collider.Volume;
            const bool isTrigger = collider.IsTrigger;
            rb.RuntimeCollisionShape = attachedShape;

            constexpr float NoColliderMass = 1.0f;
            // Bullet has no separate "type" enum the way Box2D does -- Static and Kinematic
            // both use mass 0 (Bullet's own convention for "not moved by forces"); what tells
            // them apart is the CF_KINEMATIC_OBJECT flag set below, matching Bullet's documented
            // kinematic-body recipe.
            btScalar mass = (rb.Type == BodyType::Dynamic) ? (volume > 0.0f ? density * volume : NoColliderMass) : 0.0f;
            btVector3 localInertia(0.0f, 0.0f, 0.0f);
            if (mass > 0.0f)
                attachedShape->calculateLocalInertia(mass, localInertia);

            btTransform startTransform;
            startTransform.setIdentity();
            startTransform.setOrigin(btVector3(
                PhysicsUnits::ToPhysics(worldTransform.Translation.x),
                PhysicsUnits::ToPhysics(worldTransform.Translation.y),
                PhysicsUnits::ToPhysics(worldTransform.Translation.z)));
            startTransform.setRotation(EulerDegreesToBtQuaternion(worldTransform.Rotation));

            btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
            btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, attachedShape, localInertia);
            rbInfo.m_friction = friction;
            rbInfo.m_restitution = restitution;
            btRigidBody* body = new btRigidBody(rbInfo);
            // Lets the manual manifold-diff in OnRuntimeUpdate map a touching body pair
            // back to Entities -- same static_cast<uintptr_t>(handle) convention the 2D
            // side uses via bodyDef.userData.pointer.
            body->setUserPointer(reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
            if (isTrigger) {
                // Bullet has no native sensor flag -- CF_NO_CONTACT_RESPONSE suppresses the
                // physical push-apart response while still generating contact manifolds, so
                // the manifold-diff below still detects touching (true sensor semantics).
                body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
            }
            if (rb.Type == BodyType::Kinematic) {
                // Bullet's own documented kinematic-body recipe: CF_KINEMATIC_OBJECT (moved
                // directly, e.g. by a script setting Transform, rather than by forces -- still
                // generates real collisions/pushes Dynamic bodies) plus DISABLE_DEACTIVATION,
                // since a kinematic body's mass is 0 like a Static one and Bullet would
                // otherwise treat "hasn't moved under simulation" as eligible to sleep.
                body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                body->setActivationState(DISABLE_DEACTIVATION);
            }
            world3D->World->addRigidBody(body);
            rb.RuntimeBody = body;
        }

        // A collider alone is static world geometry; Rigidbody3D is only required for
        // velocities, forces and other simulation state.  This also makes a plain 3D
        // collider targetable by PhysicsRaycaster3D/pointer events.
        auto transform3DView = m_Registry.view<TransformComponent>();
        for (auto handle : transform3DView) {
            if (!selected(handle) || std::any_of(world3D->StaticColliderBodies.begin(), world3D->StaticColliderBodies.end(),
                    [handle](const Physics3DWorld::StaticColliderBody& b) { return b.Entity == handle; }))
                continue;
            if (m_Registry.all_of<Rigidbody3DComponent>(handle)
                || !HasEnabledCollider3D(m_Registry, handle)
                || !IsEffectivelyActive(Entity(handle, this)))
                continue;

            ColliderShape3D collider = BuildColliderShape3D(*this, handle, /*dynamicBody=*/false);
            btCollisionShape* shape = collider.Shape;
            const float friction = collider.Friction;
            const float restitution = collider.Restitution;
            const bool isTrigger = collider.IsTrigger;
            if (!shape)
                continue;

            TransformComponent worldTransform = GetWorldTransform(Entity(handle, this));
            btTransform startTransform;
            startTransform.setIdentity();
            startTransform.setOrigin(btVector3(
                PhysicsUnits::ToPhysics(worldTransform.Translation.x),
                PhysicsUnits::ToPhysics(worldTransform.Translation.y),
                PhysicsUnits::ToPhysics(worldTransform.Translation.z)));
            startTransform.setRotation(EulerDegreesToBtQuaternion(worldTransform.Rotation));
            btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
            btRigidBody::btRigidBodyConstructionInfo bodyInfo(0.0f, motionState, shape);
            bodyInfo.m_friction = friction;
            bodyInfo.m_restitution = restitution;
            btRigidBody* body = new btRigidBody(bodyInfo);
            body->setUserPointer(reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
            if (isTrigger)
                body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
            world3D->World->addRigidBody(body);
            world3D->StaticColliderBodies.push_back({ handle, body, shape });
        }

        // Joints and Character Controllers need every body of this batch to exist.
        PhysicsSystems::OnBodiesCreated3D(*this, world3D->World, only);
    }

    // One fixed step of the Bullet world (Scene::OnRuntimeUpdate's fixed-timestep loop); new /
    // ended touching pairs are appended to `contactEvents` (Bullet has no contact listener).
    void Scene::StepPhysics3D(float FixedDeltaTime, std::vector<PhysicsContactEvent>& contactEvents) {
        if (!m_PhysicsWorld3D)
            return;
            Physics3DWorld* world3D = PhysicsWorld3D(m_PhysicsWorld3D);

            for (auto handle : m_Registry.view<Rigidbody3DComponent>()) {
                auto& rb = m_Registry.get<Rigidbody3DComponent>(handle);
                if (!rb.RuntimeBody)
                    continue;
                btRigidBody* body = static_cast<btRigidBody*>(rb.RuntimeBody);
                bool enabled = rb.Enabled && IsEffectivelyActive(Entity(handle, this));
                body->forceActivationState(enabled ? ACTIVE_TAG : DISABLE_SIMULATION);
                if (!enabled) {
                    body->setLinearVelocity(btVector3(0, 0, 0));
                    body->setAngularVelocity(btVector3(0, 0, 0));
                }
            }

            for (const auto& staticBody : world3D->StaticColliderBodies) {
                if (!m_Registry.valid(staticBody.Entity) || !staticBody.Body)
                    continue;
                bool enabled = HasEnabledCollider3D(m_Registry, staticBody.Entity)
                    && IsEffectivelyActive(Entity(staticBody.Entity, this));
                staticBody.Body->forceActivationState(enabled ? ACTIVE_TAG : DISABLE_SIMULATION);
                if (!enabled || !m_Registry.all_of<TransformComponent>(staticBody.Entity))
                    continue;
                TransformComponent worldTransform = GetWorldTransform(Entity(staticBody.Entity, this));
                btTransform transform;
                transform.setIdentity();
                transform.setOrigin(btVector3(
                    PhysicsUnits::ToPhysics(worldTransform.Translation.x),
                    PhysicsUnits::ToPhysics(worldTransform.Translation.y),
                    PhysicsUnits::ToPhysics(worldTransform.Translation.z)));
                transform.setRotation(EulerDegreesToBtQuaternion(worldTransform.Rotation));
                staticBody.Body->setWorldTransform(transform);
                staticBody.Body->getMotionState()->setWorldTransform(transform);
            }

            // Same Kinematic push-before-step reasoning as the 2D world above -- Bullet's own
            // documented recipe for a kinematic body is to set its new transform through the
            // motion state (not the body directly), which is what the solver actually reads
            // each step for a body with CF_KINEMATIC_OBJECT set.
            for (auto handle : m_Registry.view<Rigidbody3DComponent, TransformComponent>()) {
                auto& rb = m_Registry.get<Rigidbody3DComponent>(handle);
                if (rb.Type != BodyType::Kinematic || !rb.RuntimeBody)
                    continue;
                auto& transform = m_Registry.get<TransformComponent>(handle);
                btTransform kinematicTransform;
                kinematicTransform.setIdentity();
                kinematicTransform.setOrigin(btVector3(
                PhysicsUnits::ToPhysics(transform.Translation.x),
                PhysicsUnits::ToPhysics(transform.Translation.y),
                PhysicsUnits::ToPhysics(transform.Translation.z)));
                kinematicTransform.setRotation(EulerDegreesToBtQuaternion(transform.Rotation));
                static_cast<btRigidBody*>(rb.RuntimeBody)->getMotionState()->setWorldTransform(kinematicTransform);
            }

            // maxSubSteps defaults to 1 and fixedTimeStep defaults to 1/60 -- since the
            // timeStep passed in is now ALREADY exactly 1/60 (this loop's own FixedDeltaTime),
            // this always resolves to exactly one internal Bullet step, matching the pre-fix
            // call shape exactly, just fed a fixed instead of a real/variable timeStep.
            // Hinge springs, Spring Joints, Constant Force, Character Controller bodies.
            PhysicsSystems::PreStep3D(*this, world3D->World, FixedDeltaTime);

            world3D->World->stepSimulation(FixedDeltaTime);

            auto body3DView = m_Registry.view<Rigidbody3DComponent, TransformComponent>();
            for (auto handle : body3DView) {
                auto& rb = body3DView.get<Rigidbody3DComponent>(handle);
                auto& transform = body3DView.get<TransformComponent>(handle);
                if (!rb.RuntimeBody)
                    continue;
                btRigidBody* body = static_cast<btRigidBody*>(rb.RuntimeBody);
                btTransform worldTransform;
                body->getMotionState()->getWorldTransform(worldTransform);
                const btVector3& position = worldTransform.getOrigin();
                // Same identity-parent-only limitation as the 2D sync-back above.
                transform.Translation.x = PhysicsUnits::ToWorld(position.x());
                transform.Translation.y = PhysicsUnits::ToWorld(position.y());
                transform.Translation.z = PhysicsUnits::ToWorld(position.z());
                transform.Rotation = BtQuaternionToEulerDegrees(worldTransform.getRotation());
            }

            // Joint feedback / break detection (OnJointBreak), hinge angle readouts.
            PhysicsSystems::PostStep3D(*this, world3D->World, FixedDeltaTime);

            // No BeginContact/EndContact listener exists for Bullet (unlike Box2D above) --
            // "who is touching right now" is diffed against last frame's set instead, built
            // fresh from the dispatcher's own contact manifolds every step.
            std::set<std::pair<entt::entity, entt::entity>> currentPairs;
            btDispatcher* dispatcher = world3D->World->getDispatcher();
            int numManifolds = dispatcher->getNumManifolds();
            for (int i = 0; i < numManifolds; i++) {
                btPersistentManifold* manifold = dispatcher->getManifoldByIndexInternal(i);
                if (manifold->getNumContacts() == 0)
                    continue;
                entt::entity a = static_cast<entt::entity>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(manifold->getBody0()->getUserPointer())));
                entt::entity b = static_cast<entt::entity>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(manifold->getBody1()->getUserPointer())));
                currentPairs.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
            }

            auto is3DTrigger = [this](entt::entity handle) {
                if (m_Registry.all_of<BoxCollider3DComponent>(handle))
                    return m_Registry.get<BoxCollider3DComponent>(handle).IsTrigger;
                if (m_Registry.all_of<SphereCollider3DComponent>(handle))
                    return m_Registry.get<SphereCollider3DComponent>(handle).IsTrigger;
                if (m_Registry.all_of<CapsuleCollider3DComponent>(handle))
                    return m_Registry.get<CapsuleCollider3DComponent>(handle).IsTrigger;
                return PhysicsSystems::IsTrigger3D(m_Registry, handle); // Mesh Collider
            };
            for (auto& pair : currentPairs) {
                if (world3D->TouchingPairs.find(pair) == world3D->TouchingPairs.end())
                    contactEvents.push_back({ pair.first, pair.second, is3DTrigger(pair.first) || is3DTrigger(pair.second), true });
            }
            for (auto& pair : world3D->TouchingPairs) {
                if (currentPairs.find(pair) == currentPairs.end())
                    contactEvents.push_back({ pair.first, pair.second, is3DTrigger(pair.first) || is3DTrigger(pair.second), false });
            }
            world3D->TouchingPairs = std::move(currentPairs);
    }

    // Removes `handle`'s Bullet body / static collider body (and its joints, controller object).
    void Scene::ReleasePhysics3D(entt::entity handle) {
        if (!m_PhysicsWorld3D)
            return;
            Physics3DWorld* world3D = PhysicsWorld3D(m_PhysicsWorld3D);
            // Constraints referencing this body and its Character Controller object go first.
            PhysicsSystems::ReleaseEntity3D(*this, world3D->World, handle);
            if (auto* rb = m_Registry.try_get<Rigidbody3DComponent>(handle)) {
                if (rb->RuntimeBody) {
                    btRigidBody* body = static_cast<btRigidBody*>(rb->RuntimeBody);
                    world3D->World->removeRigidBody(body);
                    delete body->getMotionState();
                    delete body;
                    rb->RuntimeBody = nullptr;
                }
                if (rb->RuntimeCollisionShape) {
                    DeleteCollisionShape3D(static_cast<btCollisionShape*>(rb->RuntimeCollisionShape));
                    rb->RuntimeCollisionShape = nullptr;
                }
            }
            auto& statics = world3D->StaticColliderBodies;
            for (auto it = statics.begin(); it != statics.end();) {
                if (it->Entity == handle) {
                    if (it->Body) {
                        world3D->World->removeRigidBody(it->Body);
                        delete it->Body->getMotionState();
                        delete it->Body;
                        DeleteCollisionShape3D(it->CollisionShape);
                    }
                    it = statics.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it = world3D->TouchingPairs.begin(); it != world3D->TouchingPairs.end();) {
                if (it->first == handle || it->second == handle)
                    it = world3D->TouchingPairs.erase(it);
                else
                    ++it;
            }
    }

    void Scene::DestroyPhysicsWorld3D() {
        if (!m_PhysicsWorld3D)
            return;
            Physics3DWorld* world3D = PhysicsWorld3D(m_PhysicsWorld3D);
            // Constraints and Character Controller objects before the bodies they reference.
            PhysicsSystems::Shutdown3D(*this, world3D->World);

            // Bodies (and their motion states/collision shapes) are owned by us, not
            // the world -- removeRigidBody only unregisters, so each must be deleted
            // explicitly, same "we allocated it, we free it" contract as Box2D's
            // fixtures above (which the b2World itself frees, unlike here).
            auto body3DView = m_Registry.view<Rigidbody3DComponent>();
            for (auto handle : body3DView) {
                auto& rb = body3DView.get<Rigidbody3DComponent>(handle);
                if (rb.RuntimeBody) {
                    btRigidBody* body = static_cast<btRigidBody*>(rb.RuntimeBody);
                    world3D->World->removeRigidBody(body);
                    delete body->getMotionState();
                    delete body;
                    rb.RuntimeBody = nullptr;
                }
                if (rb.RuntimeCollisionShape) {
                    auto* shape = static_cast<btCollisionShape*>(rb.RuntimeCollisionShape);
                    DeleteCollisionShape3D(shape);
                    rb.RuntimeCollisionShape = nullptr;
                }
            }

            for (const auto& staticBody : world3D->StaticColliderBodies) {
                if (!staticBody.Body)
                    continue;
                world3D->World->removeRigidBody(staticBody.Body);
                delete staticBody.Body->getMotionState();
                delete staticBody.Body;
                DeleteCollisionShape3D(staticBody.CollisionShape);
            }
            world3D->StaticColliderBodies.clear();

            delete world3D->World;
            delete world3D->Solver;
            delete world3D->Broadphase;
            delete world3D->Dispatcher;
            delete world3D->CollisionConfig;
            delete world3D;
            m_PhysicsWorld3D = nullptr;
    }

    void* Scene::GetNativePhysicsWorld3D() const {
        return m_PhysicsWorld3D ? static_cast<void*>(PhysicsWorld3D(m_PhysicsWorld3D)->World) : nullptr;
    }

}
