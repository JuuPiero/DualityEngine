// Box2D components beyond bodies and basic colliders: Edge Collider 2D, the nine Unity Joint2D types,
// the five Effector2D types and Constant Force 2D. Driven from Scene.cpp through the hooks in
// Scene/PhysicsSystems.h. Per-step work is allocation-free (fixed scratch arrays, views).
#include "DualityEngine/Scene/PhysicsSystems.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <unordered_map>

#include <box2d/box2d.h>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/PhysicsMaterialLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality::PhysicsSystems {

    namespace {

        constexpr float Pi = 3.14159265358979f;
        constexpr float DegToRad = Pi / 180.0f;

        // Per-Scene runtime state (a Scene only owns a b2World between OnRuntimeStart/Stop).
        struct State2D {
            b2Body* Ground = nullptr;
            // Contacts a one-way platform let through; re-enabled when they end (EndContact).
            std::vector<b2Contact*> PassThrough;
            std::vector<PendingCallback> Breaks;
            uint32_t Random = 0x2545F491u;
        };
        std::unordered_map<const Scene*, State2D> s_States;

        State2D& StateFor(Scene& scene) { return s_States[&scene]; }

        float RandomSigned(State2D& state) {
            state.Random = state.Random * 1664525u + 1013904223u;
            return static_cast<float>((state.Random >> 8) & 0xFFFF) / 32767.5f - 1.0f;
        }

        // Unity angles: 0 = +X, 90 = up. The world is +Y down, so up is -Y.
        b2Vec2 DirectionFromDegrees(float degrees) {
            const float r = degrees * DegToRad;
            return b2Vec2(std::cos(r), -std::sin(r));
        }

        b2Vec2 ToB2(const glm::vec2& v) { return b2Vec2(PhysicsUnits::ToPhysics(v.x), PhysicsUnits::ToPhysics(v.y)); }
        glm::vec2 FromB2(const b2Vec2& v) { return { PhysicsUnits::ToWorld(v.x), PhysicsUnits::ToWorld(v.y) }; }

        entt::entity EntityOf(b2Body* body) {
            return static_cast<entt::entity>(static_cast<uint32_t>(body->GetUserData().pointer));
        }

        // The body representing `entity`: its Rigidbody2D, else the internal static body Scene
        // creates for collider-only entities (found by user data -- only at joint creation).
        b2Body* BodyOf(Scene& scene, b2World* world, entt::entity entity) {
            auto& registry = scene.Registry();
            if (!registry.valid(entity))
                return nullptr;
            if (auto* rb = registry.try_get<Rigidbody2DComponent>(entity))
                return static_cast<b2Body*>(rb->RuntimeBody);
            State2D& state = StateFor(scene);
            for (b2Body* body = world->GetBodyList(); body; body = body->GetNext())
                if (body != state.Ground && EntityOf(body) == entity)
                    return body;
            return nullptr;
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

        // ------------------------------------------------------------------ joints
        template<typename Fn>
        void ForEachJoint2D(entt::registry& registry, Fn&& fn) {
            for (auto e : registry.view<DistanceJoint2DComponent>()) fn(e, registry.get<DistanceJoint2DComponent>(e));
            for (auto e : registry.view<HingeJoint2DComponent>()) fn(e, registry.get<HingeJoint2DComponent>(e));
            for (auto e : registry.view<SpringJoint2DComponent>()) fn(e, registry.get<SpringJoint2DComponent>(e));
            for (auto e : registry.view<FixedJoint2DComponent>()) fn(e, registry.get<FixedJoint2DComponent>(e));
            for (auto e : registry.view<SliderJoint2DComponent>()) fn(e, registry.get<SliderJoint2DComponent>(e));
            for (auto e : registry.view<WheelJoint2DComponent>()) fn(e, registry.get<WheelJoint2DComponent>(e));
            for (auto e : registry.view<FrictionJoint2DComponent>()) fn(e, registry.get<FrictionJoint2DComponent>(e));
            for (auto e : registry.view<RelativeJoint2DComponent>()) fn(e, registry.get<RelativeJoint2DComponent>(e));
            for (auto e : registry.view<TargetJoint2DComponent>()) fn(e, registry.get<TargetJoint2DComponent>(e));
        }

        bool HasAnyJoint2D(entt::registry& registry) {
            return !registry.view<DistanceJoint2DComponent>().empty() || !registry.view<HingeJoint2DComponent>().empty() ||
                !registry.view<SpringJoint2DComponent>().empty() || !registry.view<FixedJoint2DComponent>().empty() ||
                !registry.view<SliderJoint2DComponent>().empty() || !registry.view<WheelJoint2DComponent>().empty() ||
                !registry.view<FrictionJoint2DComponent>().empty() || !registry.view<RelativeJoint2DComponent>().empty() ||
                !registry.view<TargetJoint2DComponent>().empty();
        }

        b2Body* Ground(Scene& scene, b2World* world) {
            State2D& state = StateFor(scene);
            if (!state.Ground) {
                b2BodyDef def;
                def.type = b2_staticBody;
                def.userData.pointer = static_cast<uintptr_t>(static_cast<uint32_t>(entt::entity(entt::null)));
                state.Ground = world->CreateBody(&def);
            }
            return state.Ground;
        }

        // Resolves self/other bodies and the anchors' world positions shared by every joint type.
        struct JointFrame {
            b2Body* Self = nullptr;
            b2Body* Other = nullptr;     // connected body or the static ground body
            bool HasConnected = false;
            b2Vec2 SelfAnchorWorld;
            b2Vec2 OtherAnchorWorld;
        };

        // `autoAtConnectedOrigin`: joints that keep a distance / slide along an axis (Distance,
        // Spring, Slider, Wheel) auto-configure the connected anchor at the connected body's
        // origin -- the anchors coinciding would make their distance/axis meaningless -- while
        // pivot-style joints (Hinge, Fixed, Friction, Relative) make it coincide with Anchor.
        bool ResolveFrame(Scene& scene, b2World* world, entt::entity entity, Joint2DBase& joint, bool useConnected, JointFrame& out,
                          bool autoAtConnectedOrigin = false) {
            out.Self = BodyOf(scene, world, entity);
            if (!out.Self) {
                Log::Warn("Joint 2D on an entity without a Rigidbody 2D/collider body -- ignored.");
                return false;
            }
            out.Other = nullptr;
            if (useConnected && joint.ConnectedBody.Handle != EntityRef::Invalid) {
                out.Other = BodyOf(scene, world, static_cast<entt::entity>(joint.ConnectedBody.Handle));
                if (!out.Other)
                    Log::Warn("Joint 2D: Connected Body has no Rigidbody 2D -- attached to the world instead.");
            }
            out.HasConnected = out.Other != nullptr && out.Other != out.Self;
            if (!out.HasConnected)
                out.Other = Ground(scene, world);
            out.SelfAnchorWorld = out.Self->GetWorldPoint(ToB2(joint.Anchor));
            if (joint.AutoConfigureConnectedAnchor && autoAtConnectedOrigin && out.HasConnected) {
                out.OtherAnchorWorld = out.Other->GetPosition();
                joint.ConnectedAnchor = glm::vec2(0.0f);
            } else if (joint.AutoConfigureConnectedAnchor) {
                out.OtherAnchorWorld = out.SelfAnchorWorld;
                joint.ConnectedAnchor = FromB2(out.HasConnected ? out.Other->GetLocalPoint(out.SelfAnchorWorld) : out.SelfAnchorWorld);
            } else {
                out.OtherAnchorWorld = out.HasConnected ? out.Other->GetWorldPoint(ToB2(joint.ConnectedAnchor)) : ToB2(joint.ConnectedAnchor);
            }
            return true;
        }

        void FillCommon(b2JointDef& def, entt::entity entity, const Joint2DBase& joint) {
            def.collideConnected = joint.EnableCollision;
            def.userData.pointer = static_cast<uintptr_t>(static_cast<uint32_t>(entity)) + 1u;
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, DistanceJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f, true)) return nullptr;
            b2DistanceJointDef def;
            def.bodyA = f.Other; def.bodyB = f.Self;
            def.localAnchorA = f.Other->GetLocalPoint(f.OtherAnchorWorld);
            def.localAnchorB = ToB2(c.Anchor);
            if (c.AutoConfigureDistance)
                c.Distance = PhysicsUnits::ToWorld((f.SelfAnchorWorld - f.OtherAnchorWorld).Length());
            def.length = std::max(PhysicsUnits::ToPhysics(c.Distance), b2_linearSlop);
            def.minLength = c.MaxDistanceOnly ? b2_linearSlop : def.length;
            def.maxLength = def.length;
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, SpringJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f, true)) return nullptr;
            b2DistanceJointDef def;
            def.bodyA = f.Other; def.bodyB = f.Self;
            def.localAnchorA = f.Other->GetLocalPoint(f.OtherAnchorWorld);
            def.localAnchorB = ToB2(c.Anchor);
            if (c.AutoConfigureDistance)
                c.Distance = PhysicsUnits::ToWorld((f.SelfAnchorWorld - f.OtherAnchorWorld).Length());
            def.length = std::max(PhysicsUnits::ToPhysics(c.Distance), b2_linearSlop);
            if (c.Frequency > 0.0f) {
                b2LinearStiffness(def.stiffness, def.damping, c.Frequency, c.DampingRatio, def.bodyA, def.bodyB);
                def.minLength = b2_linearSlop;
                def.maxLength = FLT_MAX;
            } else {
                def.minLength = def.maxLength = def.length;
            }
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, HingeJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f)) return nullptr;
            b2RevoluteJointDef def;
            def.bodyA = f.Other; def.bodyB = f.Self;
            def.localAnchorA = f.Other->GetLocalPoint(f.OtherAnchorWorld);
            def.localAnchorB = ToB2(c.Anchor);
            def.referenceAngle = f.Self->GetAngle() - f.Other->GetAngle();
            def.enableMotor = c.UseMotor;
            def.motorSpeed = c.MotorSpeed * DegToRad;
            def.maxMotorTorque = c.MaxMotorTorque;
            def.enableLimit = c.UseLimits;
            def.lowerAngle = std::min(c.LowerAngle, c.UpperAngle) * DegToRad;
            def.upperAngle = std::max(c.LowerAngle, c.UpperAngle) * DegToRad;
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, FixedJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f)) return nullptr;
            b2WeldJointDef def;
            def.bodyA = f.Other; def.bodyB = f.Self;
            def.localAnchorA = f.Other->GetLocalPoint(f.OtherAnchorWorld);
            def.localAnchorB = ToB2(c.Anchor);
            def.referenceAngle = f.Self->GetAngle() - f.Other->GetAngle();
            if (c.Frequency > 0.0f)
                b2AngularStiffness(def.stiffness, def.damping, c.Frequency, c.DampingRatio, def.bodyA, def.bodyB);
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, SliderJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f, true)) return nullptr;
            if (c.AutoConfigureAngle && f.HasConnected) {
                const b2Vec2 delta = f.Self->GetWorldCenter() - f.Other->GetWorldCenter();
                if (delta.LengthSquared() > b2_linearSlop * b2_linearSlop)
                    c.Angle = std::atan2(-delta.y, delta.x) / DegToRad;
            }
            b2PrismaticJointDef def;
            def.bodyA = f.Other; def.bodyB = f.Self;
            def.localAnchorA = f.Other->GetLocalPoint(f.OtherAnchorWorld);
            def.localAnchorB = ToB2(c.Anchor);
            b2Vec2 axis = f.Other->GetLocalVector(DirectionFromDegrees(c.Angle));
            axis.Normalize();
            def.localAxisA = axis;
            def.referenceAngle = f.Self->GetAngle() - f.Other->GetAngle();
            def.enableMotor = c.UseMotor;
            def.motorSpeed = PhysicsUnits::ToPhysics(c.MotorSpeed);
            def.maxMotorForce = c.MaxMotorForce;
            def.enableLimit = c.UseLimits;
            def.lowerTranslation = PhysicsUnits::ToPhysics(std::min(c.LowerTranslation, c.UpperTranslation));
            def.upperTranslation = PhysicsUnits::ToPhysics(std::max(c.LowerTranslation, c.UpperTranslation));
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, WheelJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f, true)) return nullptr;
            // Unity convention: the Wheel Joint 2D sits on the vehicle body, Connected Body is the
            // wheel -- so here body A is this entity and body B the wheel.
            // Auto-configured with the default Anchor: the wheel turns around its own center.
            if (c.AutoConfigureConnectedAnchor && f.HasConnected && c.Anchor == glm::vec2(0.0f))
                c.Anchor = FromB2(f.Self->GetLocalPoint(f.OtherAnchorWorld));
            b2WheelJointDef def;
            def.bodyA = f.Self; def.bodyB = f.Other;
            def.localAnchorA = ToB2(c.Anchor);
            def.localAnchorB = f.Other->GetLocalPoint(f.OtherAnchorWorld);
            b2Vec2 axis = f.Self->GetLocalVector(DirectionFromDegrees(c.SuspensionAngle));
            axis.Normalize();
            def.localAxisA = axis;
            def.enableMotor = c.UseMotor;
            def.motorSpeed = c.MotorSpeed * DegToRad;
            def.maxMotorTorque = c.MaxMotorTorque;
            if (c.SuspensionFrequency > 0.0f)
                b2LinearStiffness(def.stiffness, def.damping, c.SuspensionFrequency, c.SuspensionDampingRatio, def.bodyA, def.bodyB);
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, FrictionJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f)) return nullptr;
            b2FrictionJointDef def;
            def.bodyA = f.Other; def.bodyB = f.Self;
            def.localAnchorA = f.Other->GetLocalPoint(f.OtherAnchorWorld);
            def.localAnchorB = ToB2(c.Anchor);
            def.maxForce = c.MaxForce;
            def.maxTorque = c.MaxTorque;
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, RelativeJoint2DComponent& c) {
            JointFrame f;
            if (!ResolveFrame(scene, world, entity, c, true, f)) return nullptr;
            b2MotorJointDef def;
            def.Initialize(f.Other, f.Self);
            if (c.AutoConfigureOffset) {
                c.LinearOffset = FromB2(def.linearOffset);
                c.AngularOffset = def.angularOffset / DegToRad;
            } else {
                def.linearOffset = ToB2(c.LinearOffset);
                def.angularOffset = c.AngularOffset * DegToRad;
            }
            def.maxForce = c.MaxForce;
            def.maxTorque = c.MaxTorque;
            def.correctionFactor = std::clamp(c.CorrectionScale, 0.0f, 1.0f);
            FillCommon(def, entity, c);
            return world->CreateJoint(&def);
        }

        b2Joint* CreateJoint(Scene& scene, b2World* world, entt::entity entity, TargetJoint2DComponent& c) {
            b2Body* self = BodyOf(scene, world, entity);
            if (!self || self->GetType() != b2_dynamicBody) {
                Log::Warn("Target Joint 2D needs a Dynamic Rigidbody 2D -- ignored.");
                return nullptr;
            }
            const b2Vec2 anchorWorld = self->GetWorldPoint(ToB2(c.Anchor));
            if (c.AutoConfigureTarget)
                c.Target = FromB2(anchorWorld);
            b2MouseJointDef def;
            def.bodyA = Ground(scene, world);
            def.bodyB = self;
            def.target = anchorWorld; // fixes the local anchor on body B
            def.maxForce = c.MaxForce;
            b2LinearStiffness(def.stiffness, def.damping, std::max(c.Frequency, 0.001f), c.DampingRatio, def.bodyA, def.bodyB);
            FillCommon(def, entity, c);
            b2MouseJoint* joint = static_cast<b2MouseJoint*>(world->CreateJoint(&def));
            joint->SetTarget(ToB2(c.Target));
            return joint;
        }

        // Pushes authored values a script may change at runtime (motors, limits, targets...).
        void Sync(b2Joint*, DistanceJoint2DComponent&) {}
        void Sync(b2Joint* joint, SpringJoint2DComponent& c) {
            auto* j = static_cast<b2DistanceJoint*>(joint);
            if (!c.AutoConfigureDistance)
                j->SetLength(std::max(PhysicsUnits::ToPhysics(c.Distance), b2_linearSlop));
            if (c.Frequency > 0.0f) {
                float stiffness, damping;
                b2LinearStiffness(stiffness, damping, c.Frequency, c.DampingRatio, j->GetBodyA(), j->GetBodyB());
                j->SetStiffness(stiffness);
                j->SetDamping(damping);
            }
        }
        void Sync(b2Joint* joint, HingeJoint2DComponent& c) {
            auto* j = static_cast<b2RevoluteJoint*>(joint);
            j->EnableMotor(c.UseMotor);
            j->SetMotorSpeed(c.MotorSpeed * DegToRad);
            j->SetMaxMotorTorque(c.MaxMotorTorque);
            j->EnableLimit(c.UseLimits);
            j->SetLimits(std::min(c.LowerAngle, c.UpperAngle) * DegToRad, std::max(c.LowerAngle, c.UpperAngle) * DegToRad);
        }
        void Sync(b2Joint*, FixedJoint2DComponent&) {}
        void Sync(b2Joint* joint, SliderJoint2DComponent& c) {
            auto* j = static_cast<b2PrismaticJoint*>(joint);
            j->EnableMotor(c.UseMotor);
            j->SetMotorSpeed(PhysicsUnits::ToPhysics(c.MotorSpeed));
            j->SetMaxMotorForce(c.MaxMotorForce);
            j->EnableLimit(c.UseLimits);
            j->SetLimits(PhysicsUnits::ToPhysics(std::min(c.LowerTranslation, c.UpperTranslation)),
                         PhysicsUnits::ToPhysics(std::max(c.LowerTranslation, c.UpperTranslation)));
        }
        void Sync(b2Joint* joint, WheelJoint2DComponent& c) {
            auto* j = static_cast<b2WheelJoint*>(joint);
            j->EnableMotor(c.UseMotor);
            j->SetMotorSpeed(c.MotorSpeed * DegToRad);
            j->SetMaxMotorTorque(c.MaxMotorTorque);
        }
        void Sync(b2Joint* joint, FrictionJoint2DComponent& c) {
            auto* j = static_cast<b2FrictionJoint*>(joint);
            j->SetMaxForce(std::max(0.0f, c.MaxForce));
            j->SetMaxTorque(std::max(0.0f, c.MaxTorque));
        }
        void Sync(b2Joint* joint, RelativeJoint2DComponent& c) {
            auto* j = static_cast<b2MotorJoint*>(joint);
            j->SetMaxForce(std::max(0.0f, c.MaxForce));
            j->SetMaxTorque(std::max(0.0f, c.MaxTorque));
            j->SetCorrectionFactor(std::clamp(c.CorrectionScale, 0.0f, 1.0f));
            j->SetLinearOffset(ToB2(c.LinearOffset));
            j->SetAngularOffset(c.AngularOffset * DegToRad);
        }
        void Sync(b2Joint* joint, TargetJoint2DComponent& c) {
            auto* j = static_cast<b2MouseJoint*>(joint);
            j->SetMaxForce(std::max(0.0f, c.MaxForce));
            const b2Vec2 target = ToB2(c.Target);
            if (!(target == j->GetTarget()))
                j->SetTarget(target); // also wakes the body
        }

        // Runtime readouts (Unity's jointAngle / jointSpeed / jointTranslation).
        void ReadBack(b2Joint*, Joint2DBase&) {}
        void ReadBack(b2Joint* joint, HingeJoint2DComponent& c) {
            auto* j = static_cast<b2RevoluteJoint*>(joint);
            c.JointAngle = j->GetJointAngle() / DegToRad;
            c.JointSpeed = j->GetJointSpeed() / DegToRad;
        }
        void ReadBack(b2Joint* joint, SliderJoint2DComponent& c) {
            auto* j = static_cast<b2PrismaticJoint*>(joint);
            c.JointTranslation = PhysicsUnits::ToWorld(j->GetJointTranslation());
            c.JointSpeed = PhysicsUnits::ToWorld(j->GetJointSpeed());
        }
        void ReadBack(b2Joint* joint, WheelJoint2DComponent& c) {
            c.JointSpeed = static_cast<b2WheelJoint*>(joint)->GetJointAngularSpeed() / DegToRad;
        }

        template<typename T>
        void CreateJointsOfType(Scene& scene, b2World* world, const std::vector<entt::entity>* only) {
            auto& registry = scene.Registry();
            for (auto entity : registry.view<T>()) {
                if (only && std::find(only->begin(), only->end(), entity) == only->end())
                    continue;
                T& joint = registry.get<T>(entity);
                if (!joint.Enabled || joint.RuntimeJoint || joint.RuntimeBroken || !scene.IsEffectivelyActive(Entity(entity, &scene)))
                    continue;
                joint.RuntimeJoint = CreateJoint(scene, world, entity, joint);
            }
        }

        // ------------------------------------------------------------------ effectors
        // Fixtures of `entity` acting as effector shapes: colliders with Used By Effector, or
        // every collider when none is marked. Returns how many were written (max 5).
        int EffectorFixtures(entt::registry& registry, entt::entity entity, b2Fixture* out[5]) {
            struct Candidate { b2Fixture* Fixture; bool Marked; };
            Candidate candidates[5];
            int count = 0;
            auto add = [&](void* fixture, bool enabled, bool marked) {
                if (fixture && enabled)
                    candidates[count++] = { static_cast<b2Fixture*>(fixture), marked };
            };
            if (auto* c = registry.try_get<BoxCollider2DComponent>(entity)) add(c->RuntimeFixture, c->Enabled, c->UsedByEffector);
            if (auto* c = registry.try_get<CircleCollider2DComponent>(entity)) add(c->RuntimeFixture, c->Enabled, c->UsedByEffector);
            if (auto* c = registry.try_get<CapsuleCollider2DComponent>(entity)) add(c->RuntimeFixture, c->Enabled, c->UsedByEffector);
            if (auto* c = registry.try_get<PolygonCollider2DComponent>(entity)) add(c->RuntimeFixture, c->Enabled, c->UsedByEffector);
            if (auto* c = registry.try_get<EdgeCollider2DComponent>(entity)) add(c->RuntimeFixture, c->Enabled, c->UsedByEffector);
            bool anyMarked = false;
            for (int i = 0; i < count; ++i)
                anyMarked |= candidates[i].Marked;
            int written = 0;
            for (int i = 0; i < count; ++i)
                if (!anyMarked || candidates[i].Marked)
                    out[written++] = candidates[i].Fixture;
            return written;
        }

        bool IsOneOf(b2Fixture* fixture, b2Fixture* const* list, int count) {
            for (int i = 0; i < count; ++i)
                if (list[i] == fixture)
                    return true;
            return false;
        }

        bool PassesMask(Scene& scene, bool useMask, uint32_t mask, entt::entity other) {
            if (!useMask)
                return true;
            if (!scene.Registry().valid(other))
                return false;
            return (mask & LayerBit(static_cast<int>(scene.ResolveEntityLayer(Entity(other, &scene))))) != 0;
        }


        // Visits every (effector fixture, other non-sensor fixture of a dynamic body) pair that
        // currently overlaps/touches, once per other fixture.
        template<typename Fn>
        void ForEachEffectorContact(entt::registry& registry, entt::entity entity, Fn&& fn) {
            b2Fixture* mine[5];
            const int mineCount = EffectorFixtures(registry, entity, mine);
            if (mineCount == 0)
                return;
            b2Body* body = mine[0]->GetBody();
            constexpr int MaxVisited = 48;
            b2Fixture* visited[MaxVisited];
            int visitedCount = 0;
            for (b2ContactEdge* edge = body->GetContactList(); edge; edge = edge->next) {
                b2Contact* contact = edge->contact;
                if (!contact->IsTouching())
                    continue;
                b2Fixture* a = contact->GetFixtureA();
                b2Fixture* b = contact->GetFixtureB();
                b2Fixture* own = a->GetBody() == body ? a : b;
                b2Fixture* other = own == a ? b : a;
                if (!IsOneOf(own, mine, mineCount) || other->IsSensor() || other->GetBody()->GetType() != b2_dynamicBody)
                    continue;
                if (IsOneOf(other, visited, visitedCount))
                    continue;
                if (visitedCount < MaxVisited)
                    visited[visitedCount++] = other;
                fn(own, other);
            }
        }

        b2Vec2 FixtureCenter(b2Fixture* fixture) {
            const b2AABB& box = fixture->GetAABB(0);
            return box.GetCenter();
        }

        float FixtureArea(b2Fixture* fixture) {
            b2MassData mass;
            fixture->GetMassData(&mass);
            if (fixture->GetDensity() > 0.0f && mass.mass > 0.0f)
                return mass.mass / fixture->GetDensity();
            const b2AABB& box = fixture->GetAABB(0);
            const b2Vec2 extents = box.upperBound - box.lowerBound;
            return extents.x * extents.y;
        }

        void ApplyDrag(b2Body* body, float drag, float angularDrag, float scale) {
            if (drag > 0.0f)
                body->ApplyForceToCenter(-drag * scale * body->GetMass() * body->GetLinearVelocity(), true);
            if (angularDrag > 0.0f)
                body->ApplyTorque(-angularDrag * scale * body->GetInertia() * body->GetAngularVelocity(), true);
        }

        void UpdateEffectors(Scene& scene, b2World* world) {
            auto& registry = scene.Registry();
            State2D& state = StateFor(scene);

            for (auto entity : registry.view<AreaEffector2DComponent>()) {
                auto& effector = registry.get<AreaEffector2DComponent>(entity);
                if (!effector.Enabled || !scene.IsEffectivelyActive(Entity(entity, &scene)))
                    continue;
                ForEachEffectorContact(registry, entity, [&](b2Fixture* own, b2Fixture* other) {
                    b2Body* target = other->GetBody();
                    if (!PassesMask(scene, effector.UseColliderMask, effector.ColliderMask, EntityOf(target)))
                        return;
                    b2Vec2 direction = DirectionFromDegrees(effector.ForceAngle);
                    if (!effector.UseGlobalAngle)
                        direction = own->GetBody()->GetWorldVector(direction);
                    const float magnitude = effector.ForceMagnitude + effector.ForceVariation * RandomSigned(state);
                    const b2Vec2 force = PhysicsUnits::ToPhysics(magnitude) * direction;
                    if (effector.ForceTarget == EffectorSelection2D::Collider)
                        target->ApplyForce(force, FixtureCenter(other), true);
                    else
                        target->ApplyForceToCenter(force, true);
                    ApplyDrag(target, effector.Drag, effector.AngularDrag, 1.0f);
                });
            }

            for (auto entity : registry.view<PointEffector2DComponent>()) {
                auto& effector = registry.get<PointEffector2DComponent>(entity);
                if (!effector.Enabled || !scene.IsEffectivelyActive(Entity(entity, &scene)))
                    continue;
                ForEachEffectorContact(registry, entity, [&](b2Fixture* own, b2Fixture* other) {
                    b2Body* target = other->GetBody();
                    if (!PassesMask(scene, effector.UseColliderMask, effector.ColliderMask, EntityOf(target)))
                        return;
                    const b2Vec2 source = effector.ForceSource == EffectorSelection2D::Collider
                        ? FixtureCenter(own) : own->GetBody()->GetWorldCenter();
                    const b2Vec2 point = effector.ForceTarget == EffectorSelection2D::Collider
                        ? FixtureCenter(other) : target->GetWorldCenter();
                    b2Vec2 delta = point - source;
                    float distance = delta.Normalize() * std::max(effector.DistanceScale, 0.0001f);
                    distance = std::max(distance, PhysicsUnits::ToPhysics(0.01f));
                    float magnitude = effector.ForceMagnitude + effector.ForceVariation * RandomSigned(state);
                    if (effector.ForceMode == EffectorForceMode2D::InverseLinear)
                        magnitude /= distance;
                    else if (effector.ForceMode == EffectorForceMode2D::InverseSquared)
                        magnitude /= distance * distance;
                    target->ApplyForce(magnitude * delta, point, true);
                    ApplyDrag(target, effector.Drag, effector.AngularDrag, 1.0f);
                });
            }

            const b2Vec2 gravity = world->GetGravity();
            for (auto entity : registry.view<BuoyancyEffector2DComponent>()) {
                auto& effector = registry.get<BuoyancyEffector2DComponent>(entity);
                if (!effector.Enabled || !scene.IsEffectivelyActive(Entity(entity, &scene)))
                    continue;
                ForEachEffectorContact(registry, entity, [&](b2Fixture* own, b2Fixture* other) {
                    b2Body* target = other->GetBody();
                    if (!PassesMask(scene, effector.UseColliderMask, effector.ColliderMask, EntityOf(target)))
                        return;
                    // Surface: a horizontal line SurfaceLevel above the effector's origin (+Y down).
                    const float surfaceY = own->GetBody()->GetPosition().y - PhysicsUnits::ToPhysics(effector.SurfaceLevel);
                    const b2AABB& box = other->GetAABB(0);
                    const float height = box.upperBound.y - box.lowerBound.y;
                    if (height <= 0.0f || box.upperBound.y <= surfaceY)
                        return; // entirely above the surface
                    const float top = std::max(box.lowerBound.y, surfaceY);
                    const float fraction = std::clamp((box.upperBound.y - top) / height, 0.0f, 1.0f);
                    const float submergedArea = FixtureArea(other) * fraction;
                    const b2Vec2 centroid(box.GetCenter().x, (top + box.upperBound.y) * 0.5f);
                    target->ApplyForce(-effector.Density * submergedArea * gravity, centroid, true);
                    if (effector.FlowMagnitude != 0.0f || effector.FlowVariation != 0.0f) {
                        const float flow = effector.FlowMagnitude + effector.FlowVariation * RandomSigned(state);
                        target->ApplyForceToCenter(PhysicsUnits::ToPhysics(flow) * fraction * DirectionFromDegrees(effector.FlowAngle), true);
                    }
                    ApplyDrag(target, effector.LinearDrag, effector.AngularDrag, fraction);
                });
            }

            for (auto entity : registry.view<ConstantForce2DComponent>()) {
                auto& constant = registry.get<ConstantForce2DComponent>(entity);
                auto* rb = registry.try_get<Rigidbody2DComponent>(entity);
                if (!constant.Enabled || !rb || !rb->RuntimeBody || !scene.IsEffectivelyActive(Entity(entity, &scene)))
                    continue;
                b2Body* body = static_cast<b2Body*>(rb->RuntimeBody);
                if (body->GetType() != b2_dynamicBody)
                    continue;
                body->ApplyForceToCenter(ToB2(constant.Force) + body->GetWorldVector(ToB2(constant.RelativeForce)), true);
                if (constant.Torque != 0.0f)
                    body->ApplyTorque(constant.Torque, true);
            }
        }

        // Which entity (if any) with a component T owns `fixture` as an effector shape.
        template<typename T>
        T* EffectorForFixture(Scene& scene, b2Fixture* fixture) {
            auto& registry = scene.Registry();
            const entt::entity entity = EntityOf(fixture->GetBody());
            if (!registry.valid(entity))
                return nullptr;
            T* effector = registry.try_get<T>(entity);
            if (!effector || !effector->Enabled)
                return nullptr;
            b2Fixture* mine[5];
            const int count = EffectorFixtures(registry, entity, mine);
            return IsOneOf(fixture, mine, count) ? effector : nullptr;
        }

        void ClearJointPointer(Scene& scene, b2Joint* joint) {
            ForEachJoint2D(scene.Registry(), [&](entt::entity, Joint2DBase& c) {
                if (c.RuntimeJoint == joint)
                    c.RuntimeJoint = nullptr;
            });
        }

        // Removes a broken joint component of the right type from the entity.
        template<typename T>
        bool RemoveIfBroken(entt::registry& registry, entt::entity entity) {
            if (auto* c = registry.try_get<T>(entity); c && c->RuntimeBroken) {
                registry.remove<T>(entity);
                return true;
            }
            return false;
        }
    }

    // ------------------------------------------------------------------------- API

    bool HasEnabledCollider2D(entt::registry& registry, entt::entity entity) {
        auto* edge = registry.try_get<EdgeCollider2DComponent>(entity);
        return edge && edge->Enabled;
    }

    void CreateColliderFixtures2D(Scene& scene, entt::entity entity, void* bodyHandle) {
        auto& registry = scene.Registry();
        auto* edge = registry.try_get<EdgeCollider2DComponent>(entity);
        if (!edge || !edge->Enabled || edge->RuntimeFixture || !bodyHandle)
            return;
        b2Body* body = static_cast<b2Body*>(bodyHandle);
        ApplyMaterial(edge->PhysicsMaterial, edge->Friction, edge->Restitution, edge->Density);
        const int count = std::clamp(edge->PointCount, 2, MaxEdgeCollider2DPoints);
        b2Vec2 points[MaxEdgeCollider2DPoints];
        int unique = 0;
        for (int i = 0; i < count; ++i) {
            const b2Vec2 p = ToB2(GetEdgePoint2D(*edge, i) + edge->Offset);
            // Box2D asserts on (nearly) coincident consecutive chain vertices.
            if (unique > 0 && b2DistanceSquared(points[unique - 1], p) <= b2_linearSlop * b2_linearSlop)
                continue;
            points[unique++] = p;
        }
        if (unique < 2) {
            Log::Warn("Edge Collider 2D needs at least two distinct points -- no collider created.");
            return;
        }
        b2FixtureDef def;
        b2EdgeShape segment;
        b2ChainShape chain;
        if (unique == 2) {
            segment.SetTwoSided(points[0], points[1]);
            segment.m_radius = std::max(b2_polygonRadius, PhysicsUnits::ToPhysics(edge->EdgeRadius));
            def.shape = &segment;
        } else {
            const b2Vec2 prev = points[0] + (points[0] - points[1]);
            const b2Vec2 next = points[unique - 1] + (points[unique - 1] - points[unique - 2]);
            chain.CreateChain(points, unique, prev, next);
            chain.m_radius = std::max(b2_polygonRadius, PhysicsUnits::ToPhysics(edge->EdgeRadius));
            def.shape = &chain;
        }
        def.density = edge->Density;
        def.friction = edge->Friction;
        def.restitution = edge->Restitution;
        def.isSensor = edge->IsTrigger;
        edge->RuntimeFixture = body->CreateFixture(&def);
    }

    void OnBodiesCreated2D(Scene& scene, void* worldHandle, const std::vector<entt::entity>* only) {
        auto& registry = scene.Registry();
        if (!worldHandle || !HasAnyJoint2D(registry))
            return;
        b2World* world = static_cast<b2World*>(worldHandle);
        CreateJointsOfType<DistanceJoint2DComponent>(scene, world, only);
        CreateJointsOfType<HingeJoint2DComponent>(scene, world, only);
        CreateJointsOfType<SpringJoint2DComponent>(scene, world, only);
        CreateJointsOfType<FixedJoint2DComponent>(scene, world, only);
        CreateJointsOfType<SliderJoint2DComponent>(scene, world, only);
        CreateJointsOfType<WheelJoint2DComponent>(scene, world, only);
        CreateJointsOfType<FrictionJoint2DComponent>(scene, world, only);
        CreateJointsOfType<RelativeJoint2DComponent>(scene, world, only);
        CreateJointsOfType<TargetJoint2DComponent>(scene, world, only);
    }

    void PreStep2D(Scene& scene, void* worldHandle, float) {
        if (!worldHandle)
            return;
        b2World* world = static_cast<b2World*>(worldHandle);
        auto& registry = scene.Registry();
        ForEachJoint2D(registry, [](entt::entity, auto& c) {
            if (c.RuntimeJoint)
                Sync(static_cast<b2Joint*>(c.RuntimeJoint), c);
        });
        UpdateEffectors(scene, world);
    }

    void PostStep2D(Scene& scene, void* worldHandle, float fixedDeltaTime) {
        if (!worldHandle)
            return;
        b2World* world = static_cast<b2World*>(worldHandle);
        auto& registry = scene.Registry();
        State2D& state = StateFor(scene);
        const float invDt = fixedDeltaTime > 0.0f ? 1.0f / fixedDeltaTime : 0.0f;
        state.Breaks.clear();

        ForEachJoint2D(registry, [&](entt::entity entity, auto& c) {
            if (!c.RuntimeJoint)
                return;
            b2Joint* joint = static_cast<b2Joint*>(c.RuntimeJoint);
            const b2Vec2 force = joint->GetReactionForce(invDt);
            const float torque = joint->GetReactionTorque(invDt);
            c.ReactionForce = FromB2(force);
            c.ReactionTorque = torque;
            ReadBack(joint, c);
            const float forceMagnitude = PhysicsUnits::ToWorld(force.Length());
            const bool breakByForce = c.BreakForce > 0.0f && forceMagnitude > c.BreakForce;
            const bool breakByTorque = c.BreakTorque > 0.0f && std::fabs(torque) > c.BreakTorque;
            if (!breakByForce && !breakByTorque)
                return;
            world->DestroyJoint(joint);
            c.RuntimeJoint = nullptr;
            c.RuntimeBroken = true;
            PendingCallback event;
            event.Entity = entity;
            event.Value = breakByForce ? forceMagnitude : std::fabs(torque);
            state.Breaks.push_back(event);
        });

        if (state.Breaks.empty())
            return;
        // Unity: OnJointBreak2D(Joint2D) on the joint's GameObject, then the joint is destroyed.
        for (const PendingCallback& event : state.Breaks) {
            if (!registry.valid(event.Entity))
                continue;
            struct Payload { entt::entity Joint; Scene* Owner; float Force; } payload{ event.Entity, &scene, event.Value };
            scene.InvokeScriptCallbacks(event.Entity, "OnJointBreak2D", [](Behaviour* behaviour, void* user) {
                auto* p = static_cast<Payload*>(user);
                behaviour->OnJointBreak2D(Entity(p->Joint, p->Owner), p->Force);
            }, &payload);
        }
        for (const PendingCallback& event : state.Breaks) {
            if (!registry.valid(event.Entity))
                continue;
            (void)(RemoveIfBroken<DistanceJoint2DComponent>(registry, event.Entity) || RemoveIfBroken<HingeJoint2DComponent>(registry, event.Entity) ||
                RemoveIfBroken<SpringJoint2DComponent>(registry, event.Entity) || RemoveIfBroken<FixedJoint2DComponent>(registry, event.Entity) ||
                RemoveIfBroken<SliderJoint2DComponent>(registry, event.Entity) || RemoveIfBroken<WheelJoint2DComponent>(registry, event.Entity) ||
                RemoveIfBroken<FrictionJoint2DComponent>(registry, event.Entity) || RemoveIfBroken<RelativeJoint2DComponent>(registry, event.Entity) ||
                RemoveIfBroken<TargetJoint2DComponent>(registry, event.Entity));
        }
        state.Breaks.clear();
    }

    void PreSolve2D(Scene& scene, void* contactHandle, const void*) {
        b2Contact* contact = static_cast<b2Contact*>(contactHandle);
        b2Fixture* a = contact->GetFixtureA();
        b2Fixture* b = contact->GetFixtureB();
        if (a->IsSensor() || b->IsSensor())
            return;
        auto& registry = scene.Registry();
        if (registry.view<PlatformEffector2DComponent>().empty() && registry.view<SurfaceEffector2DComponent>().empty())
            return;
        State2D& state = StateFor(scene);

        for (int side = 0; side < 2; ++side) {
            b2Fixture* own = side == 0 ? a : b;
            b2Fixture* other = side == 0 ? b : a;

            if (auto* platform = EffectorForFixture<PlatformEffector2DComponent>(scene, own)) {
                if (!PassesMask(scene, platform->UseColliderMask, platform->ColliderMask, EntityOf(other->GetBody())))
                    continue;
                if (std::find(state.PassThrough.begin(), state.PassThrough.end(), contact) != state.PassThrough.end()) {
                    contact->SetEnabled(false);
                    return;
                }
                b2WorldManifold manifold;
                contact->GetWorldManifold(&manifold);
                // Normal from the platform towards the other body.
                const b2Vec2 normal = side == 0 ? manifold.normal : -manifold.normal;
                const b2Vec2 up = own->GetBody()->GetWorldVector(DirectionFromDegrees(90.0f + platform->RotationalOffset));
                const float cosAngle = std::clamp(b2Dot(normal, up), -1.0f, 1.0f);
                const float angle = std::acos(cosAngle) / DegToRad;
                if (platform->UseOneWay && angle > platform->SurfaceArc * 0.5f + 0.5f) {
                    contact->SetEnabled(false);
                    state.PassThrough.push_back(contact);
                    return;
                }
                // Sides: contacts roughly perpendicular to up.
                if (std::fabs(angle - 90.0f) <= std::max(platform->SideArc, 1.0f) * 0.5f + 1.0f) {
                    if (!platform->UseSideFriction)
                        contact->SetFriction(0.0f);
                    if (!platform->UseSideBounce)
                        contact->SetRestitution(0.0f);
                }
            }

            if (auto* surface = EffectorForFixture<SurfaceEffector2DComponent>(scene, own)) {
                if (!PassesMask(scene, surface->UseColliderMask, surface->ColliderMask, EntityOf(other->GetBody())))
                    continue;
                b2WorldManifold manifold;
                contact->GetWorldManifold(&manifold);
                const b2Vec2 tangent(manifold.normal.y, -manifold.normal.x);
                const b2Vec2 surfaceX = own->GetBody()->GetWorldVector(b2Vec2(1.0f, 0.0f));
                const float alignment = b2Dot(tangent, surfaceX);
                const float speed = PhysicsUnits::ToPhysics(surface->Speed + surface->SpeedVariation * RandomSigned(state));
                // Box2D drives (vB - vA) . tangent towards the tangent speed.
                const float sign = (alignment >= 0.0f ? 1.0f : -1.0f) * (side == 0 ? 1.0f : -1.0f);
                contact->SetTangentSpeed(speed * sign);
                if (!surface->UseFriction)
                    contact->SetFriction(std::clamp(surface->ForceScale, 0.0f, 1.0f));
                if (!surface->UseBounce)
                    contact->SetRestitution(0.0f);
            }
        }
    }

    void EndContact2D(Scene& scene, void* contactHandle) {
        auto found = s_States.find(&scene);
        if (found == s_States.end())
            return;
        auto& list = found->second.PassThrough;
        auto it = std::find(list.begin(), list.end(), static_cast<b2Contact*>(contactHandle));
        if (it != list.end()) {
            *it = list.back();
            list.pop_back();
        }
    }

    void ReleaseEntity2D(Scene& scene, void* worldHandle, entt::entity entity) {
        if (!worldHandle)
            return;
        b2World* world = static_cast<b2World*>(worldHandle);
        auto& registry = scene.Registry();
        // Joints attached to this entity's body (its own or other entities' connected to it):
        // destroy them explicitly so no component keeps a pointer Box2D is about to free.
        if (b2Body* body = BodyOf(scene, world, entity)) {
            for (b2JointEdge* edge = body->GetJointList(); edge;) {
                b2Joint* joint = edge->joint;
                edge = edge->next;
                ClearJointPointer(scene, joint);
                world->DestroyJoint(joint);
            }
        }
        if (auto* edge = registry.try_get<EdgeCollider2DComponent>(entity))
            edge->RuntimeFixture = nullptr;
    }

    void Shutdown2D(Scene& scene) {
        auto& registry = scene.Registry();
        ForEachJoint2D(registry, [](entt::entity, Joint2DBase& c) {
            c.RuntimeJoint = nullptr;
            c.RuntimeBroken = false;
        });
        for (auto entity : registry.view<EdgeCollider2DComponent>())
            registry.get<EdgeCollider2DComponent>(entity).RuntimeFixture = nullptr;
        s_States.erase(&scene);
    }

}
