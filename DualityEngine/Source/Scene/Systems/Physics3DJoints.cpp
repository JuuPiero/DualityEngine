// Bullet components beyond bodies and basic colliders: Mesh Collider shapes, Fixed / Hinge / Spring /
// Character joints, Constant Force and the Character Controller (kinematic capsule moved by
// collide-and-slide convex sweeps). Driven from Scene.cpp through Scene/PhysicsSystems.h.
#include "DualityEngine/Scene/PhysicsSystems.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unordered_map>

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btShapeHull.h>

#include <glm/gtc/quaternion.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/MeshReference.h"
#include "DualityEngine/Asset/PhysicsMaterialLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality::PhysicsSystems {

    namespace {

        constexpr float DegToRad = 3.14159265358979f / 180.0f;
        // Joints put THIS entity on side A of the Bullet hinge; its angle and motor then already
        // follow this body's own rotation about Axis (right-hand rule) -- verified by the
        // "Hinge Joint 3D" test (+90 deg/s about +Y takes +X to -Z). Kept as a named constant.
        constexpr float HingeSign = 1.0f;

        // A constraint plus the feedback block Bullet's solver fills (force/torque per step).
        struct JointRuntime {
            btTypedConstraint* Constraint = nullptr;
            btJointFeedback Feedback;
        };

        struct State3D {
            std::vector<PendingCallback> Breaks;
            std::vector<PendingCallback> Hits;
        };
        std::unordered_map<const Scene*, State3D> s_States;

        btVector3 ToBt(const glm::vec3& v) { return btVector3(PhysicsUnits::ToPhysics(v.x), PhysicsUnits::ToPhysics(v.y), PhysicsUnits::ToPhysics(v.z)); }
        glm::vec3 FromBt(const btVector3& v) { return { PhysicsUnits::ToWorld(v.x()), PhysicsUnits::ToWorld(v.y()), PhysicsUnits::ToWorld(v.z()) }; }

        btQuaternion EulerToBt(const glm::vec3& degrees) {
            btQuaternion qx(btVector3(1, 0, 0), glm::radians(degrees.x));
            btQuaternion qy(btVector3(0, 1, 0), glm::radians(degrees.y));
            btQuaternion qz(btVector3(0, 0, 1), glm::radians(degrees.z));
            return qz * qy * qx;
        }

        entt::entity EntityOf(const btCollisionObject* object) {
            return static_cast<entt::entity>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object->getUserPointer())));
        }

        void ApplyMaterial(const AssetRef& ref, float& friction, float& restitution, float& density) {
            if (ref.Guid.empty())
                return;
            const std::string path = AssetDatabase::ResolvePath(ref.Guid);
            if (path.empty())
                return;
            PhysicsMaterial material = PhysicsMaterialLoader::Load(path);
            friction = material.Friction;
            restitution = material.Restitution;
            density = material.Density;
        }


        const std::vector<MeshVertex>* MeshVerticesFor(Scene& scene, entt::entity entity, const MeshColliderComponent& collider) {
            // Unity: an empty Mesh Collider mesh uses the Mesh Filter's mesh.
            AssetRef meshRef = collider.Mesh;
            const MeshFilterComponent* filter = scene.Registry().try_get<MeshFilterComponent>(entity);
            if (meshRef.Guid.empty() && filter)
                meshRef = filter->Mesh;
            return MeshReference::Vertices(meshRef);
        }

        btRigidBody* BodyOf(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity) {
            auto& registry = scene.Registry();
            if (!registry.valid(entity))
                return nullptr;
            if (auto* rb = registry.try_get<Rigidbody3DComponent>(entity))
                return static_cast<btRigidBody*>(rb->RuntimeBody);
            btCollisionObjectArray& objects = world->getCollisionObjectArray();
            for (int i = 0; i < objects.size(); ++i) {
                btRigidBody* body = btRigidBody::upcast(objects[i]);
                if (body && EntityOf(body) == entity)
                    return body;
            }
            return nullptr;
        }

        template<typename Fn>
        void ForEachJoint3D(entt::registry& registry, Fn&& fn) {
            for (auto e : registry.view<FixedJointComponent>()) fn(e, registry.get<FixedJointComponent>(e));
            for (auto e : registry.view<HingeJointComponent>()) fn(e, registry.get<HingeJointComponent>(e));
            for (auto e : registry.view<SpringJointComponent>()) fn(e, registry.get<SpringJointComponent>(e));
            for (auto e : registry.view<CharacterJointComponent>()) fn(e, registry.get<CharacterJointComponent>(e));
        }

        struct JointBodies {
            btRigidBody* Self = nullptr;
            btRigidBody* Other = nullptr; // connected body or Bullet's shared fixed body
            bool HasConnected = false;
            btVector3 PivotInSelf;
            btVector3 PivotInOther;
        };

        bool ResolveBodies(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity, Joint3DBase& joint, JointBodies& out) {
            out.Self = BodyOf(scene, world, entity);
            if (!out.Self) {
                Log::Warn("Joint on an entity without a Rigidbody 3D/collider body -- ignored.");
                return false;
            }
            out.Other = nullptr;
            if (joint.ConnectedBody.Handle != EntityRef::Invalid) {
                out.Other = BodyOf(scene, world, static_cast<entt::entity>(joint.ConnectedBody.Handle));
                if (!out.Other)
                    Log::Warn("Joint: Connected Body has no Rigidbody 3D -- attached to the world instead.");
            }
            out.HasConnected = out.Other && out.Other != out.Self;
            if (!out.HasConnected)
                out.Other = &btTypedConstraint::getFixedBody();
            out.PivotInSelf = ToBt(joint.Anchor);
            const btVector3 pivotWorld = out.Self->getWorldTransform() * out.PivotInSelf;
            if (joint.AutoConfigureConnectedAnchor) {
                out.PivotInOther = out.HasConnected ? out.Other->getWorldTransform().inverse() * pivotWorld : pivotWorld;
                joint.ConnectedAnchor = FromBt(out.PivotInOther);
            } else {
                out.PivotInOther = ToBt(joint.ConnectedAnchor);
            }
            return true;
        }

        // Frame whose X axis is `axis` (and Y as close to `secondary` as possible).
        btMatrix3x3 BasisFromAxes(btVector3 axis, btVector3 secondary) {
            if (axis.length2() < 1e-8f)
                axis = btVector3(1, 0, 0);
            axis.normalize();
            secondary = secondary - axis * axis.dot(secondary);
            if (secondary.length2() < 1e-8f) {
                btVector3 helper = std::fabs(axis.y()) < 0.9f ? btVector3(0, 1, 0) : btVector3(0, 0, 1);
                secondary = helper - axis * axis.dot(helper);
            }
            secondary.normalize();
            const btVector3 third = axis.cross(secondary);
            return btMatrix3x3(axis.x(), secondary.x(), third.x(),
                               axis.y(), secondary.y(), third.y(),
                               axis.z(), secondary.z(), third.z());
        }

        void Attach(btDiscreteDynamicsWorld* world, Joint3DBase& joint, btTypedConstraint* constraint) {
            auto* runtime = new JointRuntime();
            runtime->Constraint = constraint;
            constraint->setJointFeedback(&runtime->Feedback);
            constraint->enableFeedback(true);
            world->addConstraint(constraint, !joint.EnableCollision);
            joint.RuntimeConstraint = runtime;
        }

        void Detach(btDiscreteDynamicsWorld* world, Joint3DBase& joint) {
            auto* runtime = static_cast<JointRuntime*>(joint.RuntimeConstraint);
            if (!runtime)
                return;
            if (world && runtime->Constraint) {
                world->removeConstraint(runtime->Constraint);
                runtime->Constraint->getRigidBodyA().activate(true);
                runtime->Constraint->getRigidBodyB().activate(true);
            }
            delete runtime->Constraint;
            delete runtime;
            joint.RuntimeConstraint = nullptr;
        }

        void CreateJoint(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity, FixedJointComponent& c) {
            JointBodies b;
            if (!ResolveBodies(scene, world, entity, c, b)) return;
            btTransform frameSelf; frameSelf.setIdentity(); frameSelf.setOrigin(b.PivotInSelf);
            // Keep the current relative orientation: frameOther = Other^-1 * Self * frameSelf.
            btTransform frameOther = b.Other->getWorldTransform().inverse() * b.Self->getWorldTransform() * frameSelf;
            frameOther.setOrigin(b.PivotInOther);
            Attach(world, c, new btFixedConstraint(*b.Self, *b.Other, frameSelf, frameOther));
        }

        void CreateJoint(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity, HingeJointComponent& c) {
            JointBodies b;
            if (!ResolveBodies(scene, world, entity, c, b)) return;
            btVector3 axisSelf = ToBt(c.Axis);
            if (axisSelf.length2() < 1e-8f) axisSelf = btVector3(1, 0, 0);
            axisSelf.normalize();
            const btVector3 axisWorld = b.Self->getWorldTransform().getBasis() * axisSelf;
            const btVector3 axisOther = b.Other->getWorldTransform().getBasis().transpose() * axisWorld;
            auto* hinge = new btHingeConstraint(*b.Self, *b.Other, b.PivotInSelf, b.PivotInOther, axisSelf, axisOther);
            Attach(world, c, hinge);
        }

        void CreateJoint(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity, CharacterJointComponent& c) {
            JointBodies b;
            if (!ResolveBodies(scene, world, entity, c, b)) return;
            btTransform frameSelf(BasisFromAxes(ToBt(c.Axis), ToBt(c.SwingAxis)), b.PivotInSelf);
            // Center an asymmetric Unity twist range on the frame (Bullet's twist span is symmetric).
            const float twistMid = (c.LowTwistLimit + c.HighTwistLimit) * 0.5f * DegToRad;
            btTransform twistOffset(btQuaternion(btVector3(1, 0, 0), twistMid));
            btTransform frameOther = b.Other->getWorldTransform().inverse() * b.Self->getWorldTransform() * frameSelf * twistOffset;
            frameOther.setOrigin(b.PivotInOther);
            auto* cone = new btConeTwistConstraint(*b.Self, *b.Other, frameSelf, frameOther);
            const float twistSpan = std::max(0.0f, (c.HighTwistLimit - c.LowTwistLimit) * 0.5f) * DegToRad;
            cone->setLimit(std::max(c.Swing1Limit, 0.0f) * DegToRad, std::max(c.Swing2Limit, 0.0f) * DegToRad, twistSpan);
            Attach(world, c, cone);
        }

        void CreateJoint(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity, SpringJointComponent& c) {
            // Force-based (no Bullet constraint): only the anchors/rest range are resolved here.
            JointBodies b;
            if (!ResolveBodies(scene, world, entity, c, b)) return;
            const btVector3 pa = b.Self->getWorldTransform() * b.PivotInSelf;
            const btVector3 pb = b.HasConnected ? b.Other->getWorldTransform() * b.PivotInOther : b.PivotInOther;
            const float distance = PhysicsUnits::ToWorld((pa - pb).length());
            if (c.MinDistance == 0.0f && c.MaxDistance == 0.0f) {
                c.RuntimeRestMin = c.RuntimeRestMax = distance;
            } else {
                c.RuntimeRestMin = std::min(c.MinDistance, c.MaxDistance);
                c.RuntimeRestMax = std::max(c.MinDistance, c.MaxDistance);
            }
            // A non-null marker so Release/Shutdown treat it like the other joints.
            auto* runtime = new JointRuntime();
            c.RuntimeConstraint = runtime;
        }

        bool IsCreated(const Joint3DBase& joint) { return joint.RuntimeConstraint != nullptr; }

        template<typename T>
        void CreateJointsOfType(Scene& scene, btDiscreteDynamicsWorld* world, const std::vector<entt::entity>* only) {
            auto& registry = scene.Registry();
            for (auto entity : registry.view<T>()) {
                if (only && std::find(only->begin(), only->end(), entity) == only->end())
                    continue;
                T& joint = registry.get<T>(entity);
                if (!joint.Enabled || IsCreated(joint) || joint.RuntimeBroken || !scene.IsEffectivelyActive(Entity(entity, &scene)))
                    continue;
                CreateJoint(scene, world, entity, joint);
            }
        }

        // The Spring Joint's two bodies, re-resolved each step (cheap: a component read, or a
        // scan only for collider-only connected entities).
        void StepSpring(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity, SpringJointComponent& c, State3D& state) {
            btRigidBody* self = BodyOf(scene, world, entity);
            if (!self)
                return;
            btRigidBody* other = nullptr;
            if (c.ConnectedBody.Handle != EntityRef::Invalid)
                other = BodyOf(scene, world, static_cast<entt::entity>(c.ConnectedBody.Handle));
            const btVector3 pa = self->getWorldTransform() * ToBt(c.Anchor);
            const btVector3 pb = other ? other->getWorldTransform() * ToBt(c.ConnectedAnchor) : ToBt(c.ConnectedAnchor);
            btVector3 delta = pa - pb;
            const float length = delta.length();
            if (length < 1e-6f)
                return;
            const btVector3 direction = delta / length;
            const float lengthWorld = PhysicsUnits::ToWorld(length);
            float stretch = 0.0f;
            if (lengthWorld > c.RuntimeRestMax + c.Tolerance)
                stretch = lengthWorld - c.RuntimeRestMax;
            else if (lengthWorld < c.RuntimeRestMin - c.Tolerance)
                stretch = lengthWorld - c.RuntimeRestMin;
            const btVector3 relA = pa - self->getCenterOfMassPosition();
            btVector3 velocity = self->getVelocityInLocalPoint(relA);
            btVector3 relB(0, 0, 0);
            if (other) {
                relB = pb - other->getCenterOfMassPosition();
                velocity -= other->getVelocityInLocalPoint(relB);
            }
            const float closing = velocity.dot(direction);
            const float magnitude = c.Spring * PhysicsUnits::ToPhysics(stretch) + (stretch != 0.0f ? c.Damper * closing : 0.0f);
            const btVector3 force = -direction * magnitude;
            if (magnitude != 0.0f) {
                self->activate(true);
                self->applyForce(force, relA);
                if (other) {
                    other->activate(true);
                    other->applyForce(-force, relB);
                }
            }
            c.CurrentForce = FromBt(force);
            if (c.BreakForce > 0.0f && std::fabs(magnitude) > c.BreakForce) {
                delete static_cast<JointRuntime*>(c.RuntimeConstraint);
                c.RuntimeConstraint = nullptr;
                c.RuntimeBroken = true;
                PendingCallback event;
                event.Entity = entity;
                event.Value = std::fabs(magnitude);
                state.Breaks.push_back(event);
            }
        }

        // ------------------------------------------------------- Character Controller
        btVector3 UpVector(btDiscreteDynamicsWorld* world) {
            const btVector3 gravity = world->getGravity();
            if (gravity.length2() < 1e-8f)
                return btVector3(0, -1, 0); // +Y is down in this engine
            return -gravity.normalized();
        }

        float CylinderHeight(const CharacterControllerComponent& cc) {
            return std::max(0.0f, cc.Height - 2.0f * cc.Radius);
        }

        glm::vec3 ControllerCenterWorld(Scene& scene, Entity entity, const CharacterControllerComponent& cc) {
            return scene.GetWorldTransform(entity).Translation + cc.Center;
        }

        struct SweepCallback : public btCollisionWorld::ClosestConvexResultCallback {
            const btCollisionObject* Self;
            btVector3 Direction;
            SweepCallback(const btVector3& from, const btVector3& to, const btCollisionObject* self)
                : ClosestConvexResultCallback(from, to), Self(self), Direction((to - from).normalized()) {
                m_collisionFilterGroup = btBroadphaseProxy::CharacterFilter;
                m_collisionFilterMask = btBroadphaseProxy::AllFilter;
            }
            btScalar addSingleResult(btCollisionWorld::LocalConvexResult& result, bool normalInWorldSpace) override {
                const btCollisionObject* object = result.m_hitCollisionObject;
                if (object == Self || (object->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
                    return 1.0f; // self / triggers never block
                const btVector3 normal = normalInWorldSpace ? result.m_hitNormalLocal
                    : object->getWorldTransform().getBasis() * result.m_hitNormalLocal;
                if (normal.dot(Direction) > 0.0f)
                    return 1.0f; // moving away from this surface (lets an overlapping capsule escape)
                return ClosestConvexResultCallback::addSingleResult(result, normalInWorldSpace);
            }
        };

        struct MoveContext {
            Scene* Owner;
            btDiscreteDynamicsWorld* World;
            btCollisionObject* Object;
            btConvexShape* Shape;
            CharacterControllerComponent* Controller;
            entt::entity Entity;
            btVector3 Up;
            float SkinWidth;
            float SlopeCos;
            float HalfCylinder;       // contact-point classification (Below / Sides / Above)
            bool Stepping = false;    // this Move lifted by Step Offset
            int Flags = CollisionNone;
            btVector3 MoveDirection;
            float MoveLength;
            State3D* State;
        };

        void RecordHit(MoveContext& m, const SweepCallback& hit) {
            if (m.State->Hits.size() >= 8)
                return;
            PendingCallback event;
            event.Entity = m.Entity;
            event.Other = EntityOf(hit.m_hitCollisionObject);
            event.Point = FromBt(hit.m_hitPointWorld);
            event.Normal = { hit.m_hitNormalWorld.x(), hit.m_hitNormalWorld.y(), hit.m_hitNormalWorld.z() };
            event.Direction = { m.MoveDirection.x(), m.MoveDirection.y(), m.MoveDirection.z() };
            event.Value = PhysicsUnits::ToWorld(m.MoveLength);
            m.State->Hits.push_back(event);
        }

        // Moves `position` by `delta`, sliding along what it hits (up to 4 planes). `mode`:
        // 0 = sideways (walkable slopes climb, steep ones block), 1 = up, 2 = down (ground probe).
        // Returns true if anything was hit; outGroundHit is set for a walkable hit going down.
        bool SweepAndSlide(MoveContext& m, btVector3& position, btVector3 delta, int mode, bool* outWalkable = nullptr) {
            bool anyHit = false;
            const btQuaternion rotation = btQuaternion::getIdentity();
            for (int iteration = 0; iteration < 4; ++iteration) {
                const float length = delta.length();
                if (length < 1e-5f)
                    break;
                const btVector3 direction = delta / length;
                // Cast Skin Width further than the move, so a surface closer than the skin stops
                // the move and the capsule keeps hovering Skin Width away from everything.
                const float castLength = length + m.SkinWidth;
                const btVector3 castTarget = position + direction * castLength;
                SweepCallback callback(position, castTarget, m.Object);
                m.World->convexSweepTest(m.Shape, btTransform(rotation, position), btTransform(rotation, castTarget), callback, 0.0f);
                const float hitDistance = callback.hasHit() ? callback.m_closestHitFraction * castLength : castLength;
                if (!callback.hasHit() || hitDistance - m.SkinWidth >= length) {
                    position += delta;
                    break;
                }
                anyHit = true;
                RecordHit(m, callback);
                const float travel = std::max(0.0f, hitDistance - m.SkinWidth);
                position += direction * travel;

                // Unity's CollisionFlags come from WHERE the capsule was hit: its bottom
                // hemisphere = Below, top hemisphere = Above, the cylinder = Sides.
                const btVector3 normal = callback.m_hitNormalWorld.normalized();
                const float pointHeight = (callback.m_hitPointWorld - position).dot(m.Up);
                const bool below = pointHeight < -m.HalfCylinder;
                const bool above = pointHeight > m.HalfCylinder;
                m.Flags |= below ? CollisionBelow : (above ? CollisionAbove : CollisionSides);
                const bool walkable = normal.dot(m.Up) >= m.SlopeCos;
                if (mode == 2 && below && (walkable || m.Stepping)) {
                    // Landed -- on walkable ground, or on a step edge while climbing it (its
                    // rounded corner reports a steep normal; Unity still climbs it).
                    if (outWalkable)
                        *outWalkable = true;
                    break;
                }
                btVector3 remaining = delta - direction * travel;
                remaining -= normal * remaining.dot(normal);
                if (mode == 0 && !walkable) {
                    // A wall or too-steep slope: never climb it.
                    const float climb = remaining.dot(m.Up);
                    if (climb > 0.0f)
                        remaining -= m.Up * climb;
                }
                delta = remaining;
            }
            return anyHit;
        }

        btCollisionObject* ControllerObject(CharacterControllerComponent& cc) {
            return static_cast<btCollisionObject*>(cc.RuntimeObject);
        }

        void PlaceObject(btCollisionObject* object, const btVector3& center) {
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(center);
            object->setWorldTransform(transform);
        }

        void CreateController(Scene& scene, btDiscreteDynamicsWorld* world, entt::entity entity, CharacterControllerComponent& cc) {
            auto* shape = new btCapsuleShape(PhysicsUnits::ToPhysics(std::max(cc.Radius, 0.001f)), PhysicsUnits::ToPhysics(CylinderHeight(cc)));
            auto* object = new btCollisionObject();
            object->setCollisionShape(shape);
            object->setCollisionFlags(object->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
            object->setActivationState(DISABLE_DEACTIVATION);
            object->setUserPointer(reinterpret_cast<void*>(static_cast<uintptr_t>(entity)));
            const glm::vec3 center = ControllerCenterWorld(scene, Entity(entity, &scene), cc);
            PlaceObject(object, ToBt(center));
            world->addCollisionObject(object, btBroadphaseProxy::CharacterFilter, btBroadphaseProxy::AllFilter);
            cc.RuntimeObject = object;
            cc.RuntimeShape = shape;
            cc.RuntimeLastPosition = center;
            cc.RuntimeVelocityTime = 0.0f;
            cc.IsGrounded = false;
            cc.CollisionFlags = CollisionNone;
        }

        void DestroyController(btDiscreteDynamicsWorld* world, CharacterControllerComponent& cc) {
            if (auto* object = ControllerObject(cc)) {
                if (world)
                    world->removeCollisionObject(object);
                delete object;
            }
            delete static_cast<btCollisionShape*>(cc.RuntimeShape);
            cc.RuntimeObject = nullptr;
            cc.RuntimeShape = nullptr;
        }

        void DispatchHits(Scene& scene, State3D& state) {
            if (state.Hits.empty())
                return;
            // Copy: a handler may Move again (re-entrancy appends to state.Hits).
            PendingCallback hits[8];
            const int count = static_cast<int>(std::min<size_t>(state.Hits.size(), 8));
            std::copy(state.Hits.begin(), state.Hits.begin() + count, hits);
            state.Hits.clear();
            for (int i = 0; i < count; ++i) {
                const PendingCallback& event = hits[i];
                if (!scene.Registry().valid(event.Entity) || !scene.Registry().all_of<BehaviourComponent>(event.Entity))
                    continue;
                ControllerColliderHit hit;
                hit.Controller = Entity(event.Entity, &scene);
                hit.Other = scene.Registry().valid(event.Other) ? Entity(event.Other, &scene) : Entity{};
                hit.Point = event.Point;
                hit.Normal = event.Normal;
                hit.MoveDirection = event.Direction;
                hit.MoveLength = event.Value;
                scene.InvokeScriptCallbacks(event.Entity, "OnControllerColliderHit", [](Behaviour* behaviour, void* user) {
                    behaviour->OnControllerColliderHit(*static_cast<ControllerColliderHit*>(user));
                }, &hit);
            }
        }

        void DispatchBreaks(Scene& scene, State3D& state) {
            if (state.Breaks.empty())
                return;
            auto& registry = scene.Registry();
            for (const PendingCallback& event : state.Breaks) {
                if (!registry.valid(event.Entity))
                    continue;
                struct Payload { entt::entity Joint; Scene* Owner; float Force; } payload{ event.Entity, &scene, event.Value };
                scene.InvokeScriptCallbacks(event.Entity, "OnJointBreak", [](Behaviour* behaviour, void* user) {
                    auto* p = static_cast<Payload*>(user);
                    behaviour->OnJointBreak(Entity(p->Joint, p->Owner), p->Force);
                }, &payload);
            }
            for (const PendingCallback& event : state.Breaks) {
                if (!registry.valid(event.Entity))
                    continue;
                if (auto* c = registry.try_get<FixedJointComponent>(event.Entity); c && c->RuntimeBroken) registry.remove<FixedJointComponent>(event.Entity);
                else if (auto* h = registry.try_get<HingeJointComponent>(event.Entity); h && h->RuntimeBroken) registry.remove<HingeJointComponent>(event.Entity);
                else if (auto* s = registry.try_get<SpringJointComponent>(event.Entity); s && s->RuntimeBroken) registry.remove<SpringJointComponent>(event.Entity);
                else if (auto* k = registry.try_get<CharacterJointComponent>(event.Entity); k && k->RuntimeBroken) registry.remove<CharacterJointComponent>(event.Entity);
            }
            state.Breaks.clear();
        }
    }

    // ------------------------------------------------------------------------- API

    bool HasEnabledCollider3D(entt::registry& registry, entt::entity entity) {
        auto* mesh = registry.try_get<MeshColliderComponent>(entity);
        return mesh && mesh->Enabled;
    }

    bool IsTrigger3D(entt::registry& registry, entt::entity entity) {
        auto* mesh = registry.try_get<MeshColliderComponent>(entity);
        return mesh && mesh->IsTrigger;
    }

    void* CreateColliderShape3D(Scene& scene, entt::entity entity, bool dynamicBody,
                                float& density, float& friction, float& restitution, bool& isTrigger, float& volume) {
        auto& registry = scene.Registry();
        auto* collider = registry.try_get<MeshColliderComponent>(entity);
        if (!collider || !collider->Enabled)
            return nullptr;
        ApplyMaterial(collider->PhysicsMaterial, collider->Friction, collider->Restitution, collider->Density);
        density = collider->Density;
        friction = collider->Friction;
        restitution = collider->Restitution;
        isTrigger = collider->IsTrigger;

        const std::vector<MeshVertex>* vertices = MeshVerticesFor(scene, entity, *collider);
        if (!vertices || vertices->size() < 3)
            return nullptr;
        const glm::vec3 scale = scene.GetWorldTransform(Entity(entity, &scene)).Scale;
        glm::vec3 minP(1e30f), maxP(-1e30f);
        for (const MeshVertex& v : *vertices) {
            const glm::vec3 p = v.Position * scale;
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }
        const glm::vec3 extent = maxP - minP;
        volume = std::max(extent.x, 0.0f) * std::max(extent.y, 0.0f) * std::max(extent.z, 0.0f);

        if (dynamicBody || collider->Convex) {
            if (dynamicBody && !collider->Convex)
                Log::Warn("Mesh Collider on a Dynamic Rigidbody 3D must be Convex -- using its convex hull.");
            btConvexHullShape raw;
            for (const MeshVertex& v : *vertices)
                raw.addPoint(ToBt(v.Position * scale), false);
            raw.recalcLocalAabb();
            auto* hullShape = new btConvexHullShape();
            if (vertices->size() > 64) {
                // Reduce big meshes to a small hull: support-point cost is O(points) per query.
                btShapeHull reducer(&raw);
                reducer.buildHull(raw.getMargin());
                for (int i = 0; i < reducer.numVertices(); ++i)
                    hullShape->addPoint(reducer.getVertexPointer()[i], false);
            } else {
                for (int i = 0; i < raw.getNumPoints(); ++i)
                    hullShape->addPoint(raw.getUnscaledPoints()[i], false);
            }
            hullShape->recalcLocalAabb();
            hullShape->optimizeConvexHull();
            return hullShape;
        }

        auto* triangles = new btTriangleMesh();
        const size_t triangleCount = vertices->size() / 3;
        for (size_t t = 0; t < triangleCount; ++t) {
            triangles->addTriangle(ToBt((*vertices)[t * 3 + 0].Position * scale),
                                   ToBt((*vertices)[t * 3 + 1].Position * scale),
                                   ToBt((*vertices)[t * 3 + 2].Position * scale), false);
        }
        if (triangles->getNumTriangles() == 0) {
            delete triangles;
            return nullptr;
        }
        // Not a subclass (Bullet is built without RTTI on 3DS): Scene.cpp DeleteCollisionShape3D
        // frees the triangle data together with the shape.
        return new btBvhTriangleMeshShape(triangles, true);
    }

    void OnBodiesCreated3D(Scene& scene, void* worldHandle, const std::vector<entt::entity>* only) {
        if (!worldHandle)
            return;
        auto* world = static_cast<btDiscreteDynamicsWorld*>(worldHandle);
        auto& registry = scene.Registry();
        CreateJointsOfType<FixedJointComponent>(scene, world, only);
        CreateJointsOfType<HingeJointComponent>(scene, world, only);
        CreateJointsOfType<SpringJointComponent>(scene, world, only);
        CreateJointsOfType<CharacterJointComponent>(scene, world, only);
        for (auto entity : registry.view<CharacterControllerComponent>()) {
            if (only && std::find(only->begin(), only->end(), entity) == only->end())
                continue;
            auto& cc = registry.get<CharacterControllerComponent>(entity);
            if (!cc.RuntimeObject && cc.Enabled && scene.IsEffectivelyActive(Entity(entity, &scene)))
                CreateController(scene, world, entity, cc);
        }
    }

    void PreStep3D(Scene& scene, void* worldHandle, float fixedDeltaTime) {
        if (!worldHandle)
            return;
        auto* world = static_cast<btDiscreteDynamicsWorld*>(worldHandle);
        auto& registry = scene.Registry();
        State3D& state = s_States[&scene];
        DispatchHits(scene, state); // hits from Moves made outside a script call

        for (auto entity : registry.view<HingeJointComponent>()) {
            auto& c = registry.get<HingeJointComponent>(entity);
            auto* runtime = static_cast<JointRuntime*>(c.RuntimeConstraint);
            if (!runtime)
                continue;
            auto* hinge = static_cast<btHingeConstraint*>(runtime->Constraint);
            runtime->Feedback = btJointFeedback();
            if (c.UseLimits) {
                const float a = HingeSign * c.MinLimit * DegToRad, b = HingeSign * c.MaxLimit * DegToRad;
                hinge->setLimit(std::min(a, b), std::max(a, b), 0.9f, 0.3f, 1.0f - std::clamp(c.Bounciness, 0.0f, 1.0f));
            }
            else
                hinge->setLimit(1.0f, -1.0f); // low > high = free
            const bool motor = c.UseMotor && !c.FreeSpin;
            hinge->enableAngularMotor(motor, HingeSign * c.MotorTargetVelocity * DegToRad, c.MotorForce * fixedDeltaTime);
            if (motor || c.UseSpring)
                hinge->getRigidBodyA().activate(true);
            if (c.UseSpring && (c.Spring != 0.0f || c.Damper != 0.0f)) {
                btRigidBody& a = hinge->getRigidBodyA();
                btRigidBody& b = hinge->getRigidBodyB();
                const btVector3 axis = (a.getWorldTransform().getBasis() * hinge->getAFrame().getBasis().getColumn(2)).normalized();
                const float angle = HingeSign * hinge->getHingeAngle();
                const float speed = (a.getAngularVelocity() - b.getAngularVelocity()).dot(axis);
                const float torque = -c.Spring * (angle - c.TargetPosition * DegToRad) - c.Damper * speed;
                a.applyTorque(axis * torque);
                if (!b.isStaticOrKinematicObject())
                    b.applyTorque(-axis * torque);
            }
        }
        for (auto entity : registry.view<FixedJointComponent>())
            if (auto* runtime = static_cast<JointRuntime*>(registry.get<FixedJointComponent>(entity).RuntimeConstraint))
                runtime->Feedback = btJointFeedback();
        for (auto entity : registry.view<CharacterJointComponent>())
            if (auto* runtime = static_cast<JointRuntime*>(registry.get<CharacterJointComponent>(entity).RuntimeConstraint))
                runtime->Feedback = btJointFeedback();
        for (auto entity : registry.view<SpringJointComponent>()) {
            auto& c = registry.get<SpringJointComponent>(entity);
            if (c.RuntimeConstraint && c.Enabled)
                StepSpring(scene, world, entity, c, state);
        }

        for (auto entity : registry.view<ConstantForceComponent>()) {
            auto& constant = registry.get<ConstantForceComponent>(entity);
            auto* rb = registry.try_get<Rigidbody3DComponent>(entity);
            if (!constant.Enabled || !rb || !rb->RuntimeBody || rb->Type != BodyType::Dynamic)
                continue;
            auto* body = static_cast<btRigidBody*>(rb->RuntimeBody);
            const btMatrix3x3& basis = body->getWorldTransform().getBasis();
            body->activate(true);
            body->applyCentralForce(ToBt(constant.Force) + basis * ToBt(constant.RelativeForce));
            body->applyTorque(btVector3(constant.Torque.x, constant.Torque.y, constant.Torque.z) +
                              basis * btVector3(constant.RelativeTorque.x, constant.RelativeTorque.y, constant.RelativeTorque.z));
        }

        // Character Controllers follow their Transform (a script may have teleported them).
        for (auto entity : registry.view<CharacterControllerComponent>()) {
            auto& cc = registry.get<CharacterControllerComponent>(entity);
            btCollisionObject* object = ControllerObject(cc);
            if (!object)
                continue;
            const bool active = cc.Enabled && scene.IsEffectivelyActive(Entity(entity, &scene));
            object->setCollisionFlags(active ? (object->getCollisionFlags() & ~btCollisionObject::CF_NO_CONTACT_RESPONSE)
                                             : (object->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE));
            const glm::vec3 center = ControllerCenterWorld(scene, Entity(entity, &scene), cc);
            PlaceObject(object, ToBt(center));
            cc.RuntimeVelocityTime += fixedDeltaTime;
            if (center != cc.RuntimeLastPosition) {
                cc.Velocity = (center - cc.RuntimeLastPosition) / std::max(cc.RuntimeVelocityTime, 1e-4f);
                cc.RuntimeLastPosition = center;
                cc.RuntimeVelocityTime = 0.0f;
            } else if (cc.RuntimeVelocityTime > 0.1f) {
                cc.Velocity = glm::vec3(0.0f);
            }
        }
    }

    void PostStep3D(Scene& scene, void* worldHandle, float) {
        if (!worldHandle)
            return;
        auto* world = static_cast<btDiscreteDynamicsWorld*>(worldHandle);
        auto& registry = scene.Registry();
        State3D& state = s_States[&scene];

        ForEachJoint3D(registry, [&](entt::entity entity, auto& c) {
            auto* runtime = static_cast<JointRuntime*>(c.RuntimeConstraint);
            if (!runtime || !runtime->Constraint)
                return;
            const btVector3 force = runtime->Feedback.m_appliedForceBodyA;
            const btVector3 torque = runtime->Feedback.m_appliedTorqueBodyA;
            c.CurrentForce = FromBt(force);
            c.CurrentTorque = { torque.x(), torque.y(), torque.z() };
            const float forceMagnitude = PhysicsUnits::ToWorld(force.length());
            const float torqueMagnitude = torque.length();
            const bool breakByForce = c.BreakForce > 0.0f && forceMagnitude > c.BreakForce;
            const bool breakByTorque = c.BreakTorque > 0.0f && torqueMagnitude > c.BreakTorque;
            if (!breakByForce && !breakByTorque)
                return;
            Detach(world, c);
            c.RuntimeBroken = true;
            PendingCallback event;
            event.Entity = entity;
            event.Value = breakByForce ? forceMagnitude : torqueMagnitude;
            state.Breaks.push_back(event);
        });

        for (auto entity : registry.view<HingeJointComponent>()) {
            auto& c = registry.get<HingeJointComponent>(entity);
            auto* runtime = static_cast<JointRuntime*>(c.RuntimeConstraint);
            if (!runtime || !runtime->Constraint)
                continue;
            auto* hinge = static_cast<btHingeConstraint*>(runtime->Constraint);
            c.Angle = HingeSign * hinge->getHingeAngle() / DegToRad;
            const btVector3 axis = (hinge->getRigidBodyA().getWorldTransform().getBasis() * hinge->getAFrame().getBasis().getColumn(2)).normalized();
            c.Velocity = (hinge->getRigidBodyA().getAngularVelocity() - hinge->getRigidBodyB().getAngularVelocity()).dot(axis) / DegToRad;
        }

        DispatchBreaks(scene, state);
    }

    void ReleaseEntity3D(Scene& scene, void* worldHandle, entt::entity entity) {
        auto* world = static_cast<btDiscreteDynamicsWorld*>(worldHandle);
        auto& registry = scene.Registry();
        btRigidBody* body = world ? BodyOf(scene, world, entity) : nullptr;
        // Own joints, and other entities' joints connected to this body.
        ForEachJoint3D(registry, [&](entt::entity owner, auto& c) {
            auto* runtime = static_cast<JointRuntime*>(c.RuntimeConstraint);
            if (!runtime)
                return;
            const bool attached = owner == entity || (runtime->Constraint && body &&
                (&runtime->Constraint->getRigidBodyA() == body || &runtime->Constraint->getRigidBodyB() == body)) ||
                c.ConnectedBody.Handle == static_cast<uint32_t>(entity);
            if (attached)
                Detach(world, c);
        });
        if (auto* cc = registry.try_get<CharacterControllerComponent>(entity))
            DestroyController(world, *cc);
    }

    void Shutdown3D(Scene& scene, void* worldHandle) {
        auto* world = static_cast<btDiscreteDynamicsWorld*>(worldHandle);
        auto& registry = scene.Registry();
        ForEachJoint3D(registry, [&](entt::entity, auto& c) {
            Detach(world, c);
            c.RuntimeBroken = false;
        });
        for (auto entity : registry.view<CharacterControllerComponent>())
            DestroyController(world, registry.get<CharacterControllerComponent>(entity));
        s_States.erase(&scene);
    }

    int CharacterMove(Scene& scene, Entity entity, const glm::vec3& motion) {
        if (!entity || !entity.HasComponent<CharacterControllerComponent>())
            return CollisionNone;
        auto& cc = entity.GetComponent<CharacterControllerComponent>();
        auto* world = static_cast<btDiscreteDynamicsWorld*>(scene.GetNativePhysicsWorld3D());
        btCollisionObject* object = ControllerObject(cc);
        auto& transform = entity.GetComponent<TransformComponent>();
        if (!cc.Enabled)
            return CollisionNone;
        if (!world || !object) {
            // Edit mode / not simulated: plain translation, like Unity outside of physics.
            TransformComponent worldTransform = scene.GetWorldTransform(entity);
            worldTransform.Translation += motion;
            transform.Translation = scene.WorldToLocal(entity, worldTransform).Translation;
            return CollisionNone;
        }
        if (glm::length(motion) < cc.MinMoveDistance) {
            return cc.CollisionFlags;
        }

        State3D& state = s_States[&scene];
        MoveContext m;
        m.Owner = &scene;
        m.World = world;
        m.Object = object;
        m.Shape = static_cast<btConvexShape*>(cc.RuntimeShape);
        m.Controller = &cc;
        m.Entity = entity.Handle();
        m.Up = UpVector(world);
        m.SkinWidth = PhysicsUnits::ToPhysics(std::max(cc.SkinWidth, 0.0f));
        m.SlopeCos = std::cos(std::clamp(cc.SlopeLimit, 0.0f, 180.0f) * DegToRad);
        m.State = &state;
        const btVector3 delta = ToBt(motion);
        m.MoveLength = delta.length();
        m.MoveDirection = m.MoveLength > 0.0f ? delta / m.MoveLength : btVector3(0, 0, 0);

        const glm::vec3 startCenter = ControllerCenterWorld(scene, entity, cc);
        btVector3 position = ToBt(startCenter);
        const float vertical = delta.dot(m.Up);
        const btVector3 horizontal = delta - m.Up * vertical;
        const bool movingSideways = horizontal.length2() > 1e-10f;
        const float stepUp = (cc.IsGrounded && movingSideways) ? PhysicsUnits::ToPhysics(std::max(cc.StepOffset, 0.0f)) : 0.0f;
        m.HalfCylinder = PhysicsUnits::ToPhysics(CylinderHeight(cc) * 0.5f);
        m.Stepping = stepUp > 0.0f;

        // 1. Up: jump motion plus the step-climb lift.
        const float upAmount = stepUp + std::max(vertical, 0.0f);
        float liftedBy = 0.0f;
        if (upAmount > 0.0f) {
            const btVector3 before = position;
            SweepAndSlide(m, position, m.Up * upAmount, 1);
            liftedBy = (position - before).dot(m.Up) - std::max(vertical, 0.0f);
            liftedBy = std::clamp(liftedBy, 0.0f, stepUp);
        }
        // 2. Sideways slide.
        if (movingSideways)
            SweepAndSlide(m, position, horizontal, 0);
        // 3. Down: fall motion plus undoing the step lift (lands on the step or the ground).
        const float downAmount = liftedBy + std::max(-vertical, 0.0f);
        bool grounded = false;
        if (downAmount > 0.0f) {
            bool walkable = false;
            SweepAndSlide(m, position, -m.Up * downAmount, 2, &walkable);
            grounded = walkable;
        }
        // Ground probe so walking down gentle slopes / standing still keeps isGrounded.
        if (!grounded && vertical <= 0.0f) {
            const btVector3 probeTarget = position - m.Up * (m.SkinWidth * 2.0f + PhysicsUnits::ToPhysics(0.01f));
            SweepCallback probe(position, probeTarget, object);
            world->convexSweepTest(m.Shape, btTransform(btQuaternion::getIdentity(), position),
                                   btTransform(btQuaternion::getIdentity(), probeTarget), probe, 0.0f);
            if (probe.hasHit() && probe.m_hitNormalWorld.normalized().dot(m.Up) >= m.SlopeCos) {
                grounded = true;
                m.Flags |= CollisionBelow;
            }
        }

        cc.CollisionFlags = m.Flags;
        cc.IsGrounded = grounded || (m.Flags & CollisionBelow) != 0;
        PlaceObject(object, position);

        TransformComponent worldTransform = scene.GetWorldTransform(entity);
        worldTransform.Translation += FromBt(position) - startCenter;
        transform.Translation = scene.WorldToLocal(entity, worldTransform).Translation;

        DispatchHits(scene, state);
        return cc.CollisionFlags;
    }

    bool CharacterSimpleMove(Scene& scene, Entity entity, const glm::vec3& speed) {
        if (!entity || !entity.HasComponent<CharacterControllerComponent>())
            return false;
        auto& cc = entity.GetComponent<CharacterControllerComponent>();
        auto* world = static_cast<btDiscreteDynamicsWorld*>(scene.GetNativePhysicsWorld3D());
        // SimpleMove is meant to be called once per frame (Update): integrate over Time.deltaTime.
        const EngineServices* services = ScriptContext::Services();
        float dt = services && services->GetDeltaTime ? services->GetDeltaTime() : 0.0f;
        if (dt <= 0.0f)
            dt = 1.0f / 60.0f;
        glm::vec3 up(0.0f, -1.0f, 0.0f);
        float gravity = PhysicsUnits::Gravity();
        if (world) {
            const btVector3 u = UpVector(world);
            up = { u.x(), u.y(), u.z() };
            gravity = world->getGravity().length();
        }
        // Unity SimpleMove: the vertical part of `speed` is ignored, gravity is applied.
        glm::vec3 horizontal = speed - up * glm::dot(speed, up);
        if (cc.IsGrounded)
            cc.SimpleMoveFallSpeed = 0.0f;
        cc.SimpleMoveFallSpeed += gravity * dt;
        const glm::vec3 motion = horizontal * dt - up * (cc.SimpleMoveFallSpeed * dt);
        CharacterMove(scene, entity, motion);
        return cc.IsGrounded;
    }

}
