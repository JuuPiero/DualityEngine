// Box2D side of a running Scene: world creation, bodies + collider fixtures, the fixed step and
// its Transform sync, raycasts / point queries and teardown. Joints, effectors and the Edge
// Collider live in Physics2DJoints.cpp (PhysicsSystems.h). Scene.cpp orchestrates the order.
#include "PhysicsWorlds.h"

namespace Duality {

    bool HasEnabledCollider2D(entt::registry& registry, entt::entity entity) {
        return (registry.all_of<BoxCollider2DComponent>(entity) && registry.get<BoxCollider2DComponent>(entity).Enabled)
            || (registry.all_of<CircleCollider2DComponent>(entity) && registry.get<CircleCollider2DComponent>(entity).Enabled)
            || (registry.all_of<CapsuleCollider2DComponent>(entity) && registry.get<CapsuleCollider2DComponent>(entity).Enabled)
            || (registry.all_of<PolygonCollider2DComponent>(entity) && registry.get<PolygonCollider2DComponent>(entity).Enabled)
            || PhysicsSystems::HasEnabledCollider2D(registry, entity);
    }

    namespace {
        // Adds a fixture for every enabled 2D collider of `entity` to `body` -- the single place
        // collider shapes are built, for Rigidbody 2D bodies and collider-only static bodies.
        void CreateColliderFixtures2D(Scene& scene, entt::entity entity, b2Body* body, float densityScale) {
            entt::registry& registry = scene.Registry();
            if (registry.all_of<BoxCollider2DComponent>(entity)) {
                auto& box = registry.get<BoxCollider2DComponent>(entity);
                if (box.Enabled) {
                    ApplyPhysicsMaterial(box.PhysicsMaterial, box.Friction, box.Restitution, box.Density);
                    b2PolygonShape shape;
                    shape.SetAsBox(PhysicsUnits::ToPhysics(box.Size.x), PhysicsUnits::ToPhysics(box.Size.y),
                                   b2Vec2(PhysicsUnits::ToPhysics(box.Offset.x), PhysicsUnits::ToPhysics(box.Offset.y)), 0.0f);
                    b2FixtureDef fixtureDef;
                    fixtureDef.shape = &shape;
                    fixtureDef.density = box.Density * densityScale;
                    fixtureDef.friction = box.Friction;
                    fixtureDef.restitution = box.Restitution;
                    fixtureDef.isSensor = box.IsTrigger;
                    box.RuntimeFixture = body->CreateFixture(&fixtureDef);
                }
            }
            if (registry.all_of<CircleCollider2DComponent>(entity)) {
                auto& circle = registry.get<CircleCollider2DComponent>(entity);
                if (circle.Enabled) {
                    ApplyPhysicsMaterial(circle.PhysicsMaterial, circle.Friction, circle.Restitution, circle.Density);
                    b2CircleShape shape;
                    shape.m_p.Set(PhysicsUnits::ToPhysics(circle.Offset.x), PhysicsUnits::ToPhysics(circle.Offset.y));
                    shape.m_radius = PhysicsUnits::ToPhysics(circle.Radius);
                    b2FixtureDef fixtureDef;
                    fixtureDef.shape = &shape;
                    fixtureDef.density = circle.Density * densityScale;
                    fixtureDef.friction = circle.Friction;
                    fixtureDef.restitution = circle.Restitution;
                    fixtureDef.isSensor = circle.IsTrigger;
                    circle.RuntimeFixture = body->CreateFixture(&fixtureDef);
                }
            }
            if (registry.all_of<CapsuleCollider2DComponent>(entity)) {
                auto& cap = registry.get<CapsuleCollider2DComponent>(entity);
                if (cap.Enabled) {
                    ApplyPhysicsMaterial(cap.PhysicsMaterial, cap.Friction, cap.Restitution, cap.Density);
                    b2PolygonShape shape = MakeCapsulePolygon2D(PhysicsUnits::ToPhysics(cap.Radius), PhysicsUnits::ToPhysics(cap.Height));
                    b2FixtureDef fixtureDef;
                    fixtureDef.shape = &shape;
                    fixtureDef.density = cap.Density * densityScale;
                    fixtureDef.friction = cap.Friction;
                    fixtureDef.restitution = cap.Restitution;
                    fixtureDef.isSensor = cap.IsTrigger;
                    cap.RuntimeFixture = body->CreateFixture(&fixtureDef);
                }
            }
            if (registry.all_of<PolygonCollider2DComponent>(entity)) {
                auto& poly = registry.get<PolygonCollider2DComponent>(entity);
                if (poly.Enabled) {
                    ApplyPhysicsMaterial(poly.PhysicsMaterial, poly.Friction, poly.Restitution, poly.Density);
                    int count = std::clamp(poly.VertexCount, 3, 8);
                    b2Vec2 verts[8];
                    for (int i = 0; i < count; ++i) {
                        glm::vec2 v = GetPolygonVertex2D(poly, i) + poly.Offset;
                        verts[i].Set(PhysicsUnits::ToPhysics(v.x), PhysicsUnits::ToPhysics(v.y));
                    }
                    b2PolygonShape shape;
                    shape.Set(verts, count);
                    b2FixtureDef fixtureDef;
                    fixtureDef.shape = &shape;
                    fixtureDef.density = poly.Density * densityScale;
                    fixtureDef.friction = poly.Friction;
                    fixtureDef.restitution = poly.Restitution;
                    fixtureDef.isSensor = poly.IsTrigger;
                    poly.RuntimeFixture = body->CreateFixture(&fixtureDef);
                }
            }
            // Edge Collider 2D (Physics2DJoints.cpp).
            PhysicsSystems::CreateColliderFixtures2D(scene, entity, body);
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

        class ClosestPointQueryCallback2D : public b2QueryCallback {
        public:
            b2Vec2 QueryPoint{};
            b2Fixture* ClosestFixture = nullptr;
            float ClosestDistSq = std::numeric_limits<float>::max();

            bool ReportFixture(b2Fixture* fixture) override {
                if (!fixture->TestPoint(QueryPoint))
                    return true;
                b2Vec2 center = fixture->GetBody()->GetWorldCenter();
                float dx = center.x - QueryPoint.x;
                float dy = center.y - QueryPoint.y;
                float distSq = dx * dx + dy * dy;
                if (distSq < ClosestDistSq) {
                    ClosestDistSq = distSq;
                    ClosestFixture = fixture;
                }
                return true;
            }
        };
    }

    void Scene::CreatePhysicsWorld2D() {
        Physics2DWorld* world2D = new Physics2DWorld();
        world2D->World = new b2World(b2Vec2(0.0f, PhysicsUnits::PhysicsGravity()));
        world2D->Listener = new Box2DContactListener();
        world2D->Listener->Owner = this;
        world2D->World->SetContactListener(world2D->Listener);
        m_PhysicsWorld = world2D;
    }

    RaycastHit2D Scene::Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) {
        RaycastHit2D result;
        if (!m_PhysicsWorld)
            return result; // not Playing -- no b2World to query yet

        glm::vec2 dir = glm::normalize(direction);
        glm::vec2 from = { PhysicsUnits::ToPhysics(origin.x), PhysicsUnits::ToPhysics(origin.y) };
        glm::vec2 to = from + dir * PhysicsUnits::ToPhysics(maxDistance);

        ClosestRayCastCallback2D callback;
        PhysicsWorld2D(m_PhysicsWorld)->World->RayCast(&callback, b2Vec2(from.x, from.y), b2Vec2(to.x, to.y));
        if (!callback.Hit)
            return result;

        // Same uintptr_t -> uint32_t -> entt::entity narrowing as Box2DContactListener::Record
        // above -- same userData convention, just read here instead of at contact time.
        entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(callback.Fixture->GetBody()->GetUserData().pointer));
        if (!m_Registry.valid(handle))
            return result;

        result.HitEntity = Entity(handle, this);
        result.Point = { PhysicsUnits::ToWorld(callback.Point.x), PhysicsUnits::ToWorld(callback.Point.y) };
        result.Normal = { callback.Normal.x, callback.Normal.y };
        result.Distance = PhysicsUnits::ToWorld(glm::distance(from, glm::vec2(callback.Point.x, callback.Point.y)));
        return result;
    }

    RaycastHit2D Scene::RaycastPoint2D(const glm::vec2& worldPoint) {
        RaycastHit2D result;
        if (!m_PhysicsWorld)
            return result;

        glm::vec2 physicsPoint = { PhysicsUnits::ToPhysics(worldPoint.x), PhysicsUnits::ToPhysics(worldPoint.y) };
        ClosestPointQueryCallback2D callback;
        callback.QueryPoint.Set(physicsPoint.x, physicsPoint.y);
        b2AABB aabb;
        aabb.lowerBound.Set(physicsPoint.x - 0.001f, physicsPoint.y - 0.001f);
        aabb.upperBound.Set(physicsPoint.x + 0.001f, physicsPoint.y + 0.001f);
        PhysicsWorld2D(m_PhysicsWorld)->World->QueryAABB(&callback, aabb);
        if (!callback.ClosestFixture)
            return result;

        entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(callback.ClosestFixture->GetBody()->GetUserData().pointer));
        if (!m_Registry.valid(handle))
            return result;

        result.HitEntity = Entity(handle, this);
        result.Point = worldPoint;
        result.Distance = 0.0f;
        return result;
    }

    // Creates the Box2D bodies for every eligible entity (`only` == nullptr) or only the listed
    // ones; entities that already have a body are skipped. See Scene::CreatePhysicsBodies.
    void Scene::CreatePhysicsBodies2D(const std::vector<entt::entity>* only) {
        if (!m_PhysicsWorld)
            return;
        auto selected = [only](entt::entity handle) {
            return !only || std::find(only->begin(), only->end(), handle) != only->end();
        };
        Physics2DWorld* world2D = PhysicsWorld2D(m_PhysicsWorld);
        b2World* world = world2D->World;

        const float densityScale = 1.0f;
        auto bodyView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
        for (auto handle : bodyView) {
            auto& rb = bodyView.get<Rigidbody2DComponent>(handle);
            if (!selected(handle) || rb.RuntimeBody)
                continue;

            // An entity that isn't effectively active when Play starts gets no physics
            // body at all (RuntimeBody stays nullptr, which every other body-consuming
            // loop already null-checks) -- scope cut: toggling Active mid-Play does NOT
            // dynamically add/remove the body, only whether it existed at Play start.
            if (!IsEffectivelyActive(Entity(handle, this)))
                continue;
            if (!rb.Enabled)
                continue;

            // Spawn at the entity's resolved WORLD transform (identity pass-through
            // for a root entity) so a child starts in the right place -- but Box2D
            // then simulates this body independently in world space afterward (see
            // OnRuntimeUpdate's sync-back below). A Rigidbody2D on a child of a
            // moving/rotating parent will NOT track that parent during simulation --
            // the same real limitation Unity documents for non-kinematic parent/child
            // Rigidbodies, not a gap unique to this engine. Keep physics entities as
            // roots, or children of a parent that stays at identity.
            TransformComponent worldTransform = GetWorldTransform(Entity(handle, this));

            b2BodyDef bodyDef;
            switch (rb.Type) {
                case BodyType::Static:    bodyDef.type = b2_staticBody; break;
                case BodyType::Kinematic: bodyDef.type = b2_kinematicBody; break;
                case BodyType::Dynamic:   default: bodyDef.type = b2_dynamicBody; break;
            }
            bodyDef.position.Set(PhysicsUnits::ToPhysics(worldTransform.Translation.x), PhysicsUnits::ToPhysics(worldTransform.Translation.y));
            // TransformComponent::Rotation is always in degrees (matching
            // Unity and the Properties panel's plain drag-float) -- Box2D's
            // own angle is radians, so the boundary conversion happens here.
            bodyDef.angle = glm::radians(worldTransform.Rotation.z);
            bodyDef.fixedRotation = rb.FixedRotation;
            // Lets Box2DContactListener map a touching fixture pair back to Entities --
            // same static_cast<uintptr_t>(handle) convention already used for
            // EngineServices_FindEntityInScreen's own entt::entity<->raw-handle boundary.
            bodyDef.userData.pointer = static_cast<uintptr_t>(handle);
            b2Body* body = world->CreateBody(&bodyDef);
            rb.RuntimeBody = body;
            if (rb.LinearDrag > 0.0f)
                body->SetLinearDamping(rb.LinearDrag);
            if (rb.AngularDrag > 0.0f)
                body->SetAngularDamping(rb.AngularDrag);

            // Same fixtures as a collider-only static body (one place builds collider shapes).
            CreateColliderFixtures2D(*this, handle, body, densityScale);
        }

        // Collider2D without Rigidbody2D is a static collider, just as in Unity.  It
        // receives an internal body only; scripts still see exactly the components they
        // authored and cannot accidentally apply forces to it through a Rigidbody API.
        auto transform2DView = m_Registry.view<TransformComponent>();
        for (auto handle : transform2DView) {
            if (!selected(handle) || std::any_of(world2D->StaticColliderBodies.begin(), world2D->StaticColliderBodies.end(),
                    [handle](const Physics2DWorld::StaticColliderBody& b) { return b.Entity == handle; }))
                continue;
            if (m_Registry.all_of<Rigidbody2DComponent>(handle)
                || !HasEnabledCollider2D(m_Registry, handle)
                || !IsEffectivelyActive(Entity(handle, this)))
                continue;

            TransformComponent worldTransform = GetWorldTransform(Entity(handle, this));
            b2BodyDef bodyDef;
            bodyDef.type = b2_staticBody;
            bodyDef.position.Set(PhysicsUnits::ToPhysics(worldTransform.Translation.x), PhysicsUnits::ToPhysics(worldTransform.Translation.y));
            bodyDef.angle = glm::radians(worldTransform.Rotation.z);
            bodyDef.userData.pointer = static_cast<uintptr_t>(handle);
            b2Body* body = world->CreateBody(&bodyDef);
            CreateColliderFixtures2D(*this, handle, body, densityScale);
            world2D->StaticColliderBodies.push_back({ handle, body });
        }

        // Joints need every body of this batch to exist.
        PhysicsSystems::OnBodiesCreated2D(*this, world, only);
    }

    // One fixed step of the Box2D world (Scene::OnRuntimeUpdate's fixed-timestep loop): pushes
    // Transform -> body for static colliders / kinematic bodies, steps, syncs body -> Transform.
    // Begin/End contacts are appended to `contactEvents`.
    void Scene::StepPhysics2D(float FixedDeltaTime, std::vector<PhysicsContactEvent>& contactEvents) {
        if (!m_PhysicsWorld)
            return;
            Physics2DWorld* world2D = PhysicsWorld2D(m_PhysicsWorld);
            world2D->Listener->Events = &contactEvents;

            for (auto handle : m_Registry.view<Rigidbody2DComponent>()) {
                auto& rb = m_Registry.get<Rigidbody2DComponent>(handle);
                if (!rb.RuntimeBody)
                    continue;
                bool enabled = rb.Enabled && IsEffectivelyActive(Entity(handle, this));
                static_cast<b2Body*>(rb.RuntimeBody)->SetEnabled(enabled);
            }

            // Static collider bodies can be moved by a script, and Active/Enabled
            // changes must affect queries immediately.  Updating these bodies is a
            // little more expensive than keeping truly immutable static geometry,
            // but is predictable and matches the component-centric editor workflow.
            for (const auto& staticBody : world2D->StaticColliderBodies) {
                if (!m_Registry.valid(staticBody.Entity) || !staticBody.Body)
                    continue;
                bool enabled = HasEnabledCollider2D(m_Registry, staticBody.Entity)
                    && IsEffectivelyActive(Entity(staticBody.Entity, this));
                staticBody.Body->SetEnabled(enabled);
                if (!enabled || !m_Registry.all_of<TransformComponent>(staticBody.Entity))
                    continue;
                TransformComponent worldTransform = GetWorldTransform(Entity(staticBody.Entity, this));
                staticBody.Body->SetTransform(
                    b2Vec2(PhysicsUnits::ToPhysics(worldTransform.Translation.x), PhysicsUnits::ToPhysics(worldTransform.Translation.y)),
                    glm::radians(worldTransform.Rotation.z));
            }

            // Kinematic bodies are moved by script/animation, not by Box2D's own solver (mass
            // 0, same as Static) -- so unlike Dynamic, the ENTITY's current TransformComponent
            // is the source of truth each frame, pushed into the body right before it steps.
            // The sync-back loop after Step() below then just reads this same value straight
            // back for a Kinematic body (nothing moved it), a harmless no-op.
            for (auto handle : m_Registry.view<Rigidbody2DComponent, TransformComponent>()) {
                auto& rb = m_Registry.get<Rigidbody2DComponent>(handle);
                if (rb.Type != BodyType::Kinematic || !rb.RuntimeBody)
                    continue;
                auto& transform = m_Registry.get<TransformComponent>(handle);
                static_cast<b2Body*>(rb.RuntimeBody)->SetTransform(
                    b2Vec2(PhysicsUnits::ToPhysics(transform.Translation.x), PhysicsUnits::ToPhysics(transform.Translation.y)),
                    glm::radians(transform.Rotation.z));
            }

            // Joint motors/targets, effectors, Constant Force 2D.
            PhysicsSystems::PreStep2D(*this, world2D->World, FixedDeltaTime);

            world2D->World->Step(FixedDeltaTime, VelocityIterations, PositionIterations);

            auto bodyView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
            for (auto handle : bodyView) {
                auto& rb = bodyView.get<Rigidbody2DComponent>(handle);
                auto& transform = bodyView.get<TransformComponent>(handle);
                if (!rb.RuntimeBody)
                    continue;
                b2Body* body = static_cast<b2Body*>(rb.RuntimeBody);
                const b2Vec2& position = body->GetPosition();
                // Box2D always simulates in world space and this writes straight into
                // the entity's own (local) TransformComponent -- correct only if this
                // entity has no parent, or its parent stays at identity. See the
                // matching comment at body-creation time in OnRuntimeStart.
                transform.Translation.x = PhysicsUnits::ToWorld(position.x);
                transform.Translation.y = PhysicsUnits::ToWorld(position.y);
                transform.Rotation.z = glm::degrees(body->GetAngle());
            }

            // Joint reaction forces / break detection (OnJointBreak2D).
            PhysicsSystems::PostStep2D(*this, world2D->World, FixedDeltaTime);
    }

    // Destroys `handle`'s Box2D body / static collider body (and the joints attached to it).
    void Scene::ReleasePhysics2D(entt::entity handle) {
        if (!m_PhysicsWorld)
            return;
            Physics2DWorld* world2D = PhysicsWorld2D(m_PhysicsWorld);
            // A destroyed body ends its contacts; nothing must record into a dead frame's list.
            world2D->Listener->Events = nullptr;
            // Joints attached to this body are destroyed first (no dangling b2Joint*).
            PhysicsSystems::ReleaseEntity2D(*this, world2D->World, handle);
            if (auto* rb = m_Registry.try_get<Rigidbody2DComponent>(handle)) {
                if (rb->RuntimeBody) {
                    world2D->World->DestroyBody(static_cast<b2Body*>(rb->RuntimeBody));
                    rb->RuntimeBody = nullptr;
                }
            }
            auto& statics = world2D->StaticColliderBodies;
            for (auto it = statics.begin(); it != statics.end();) {
                if (it->Entity == handle) {
                    if (it->Body)
                        world2D->World->DestroyBody(it->Body);
                    it = statics.erase(it);
                } else {
                    ++it;
                }
            }
            if (auto* c = m_Registry.try_get<BoxCollider2DComponent>(handle)) c->RuntimeFixture = nullptr;
            if (auto* c = m_Registry.try_get<CircleCollider2DComponent>(handle)) c->RuntimeFixture = nullptr;
            if (auto* c = m_Registry.try_get<CapsuleCollider2DComponent>(handle)) c->RuntimeFixture = nullptr;
            if (auto* c = m_Registry.try_get<PolygonCollider2DComponent>(handle)) c->RuntimeFixture = nullptr;
    }

    void Scene::DestroyPhysicsWorld2D() {
        if (!m_PhysicsWorld)
            return;
            Physics2DWorld* world2D = PhysicsWorld2D(m_PhysicsWorld);
            PhysicsSystems::Shutdown2D(*this); // joint/edge pointers die with the world
            delete world2D->World; // also frees every body/fixture it owns
            delete world2D->Listener; // caller-owned, per b2World::SetContactListener's own contract
            delete world2D;
            m_PhysicsWorld = nullptr;

            auto bodyView = m_Registry.view<Rigidbody2DComponent>();
            for (auto handle : bodyView)
                bodyView.get<Rigidbody2DComponent>(handle).RuntimeBody = nullptr;
            auto boxView = m_Registry.view<BoxCollider2DComponent>();
            for (auto handle : boxView)
                boxView.get<BoxCollider2DComponent>(handle).RuntimeFixture = nullptr;
            auto circleView = m_Registry.view<CircleCollider2DComponent>();
            for (auto handle : circleView)
                circleView.get<CircleCollider2DComponent>(handle).RuntimeFixture = nullptr;
            auto cap2DView = m_Registry.view<CapsuleCollider2DComponent>();
            for (auto handle : cap2DView)
                cap2DView.get<CapsuleCollider2DComponent>(handle).RuntimeFixture = nullptr;
            auto polyView = m_Registry.view<PolygonCollider2DComponent>();
            for (auto handle : polyView)
                polyView.get<PolygonCollider2DComponent>(handle).RuntimeFixture = nullptr;
    }

    void* Scene::GetNativePhysicsWorld2D() const {
        return m_PhysicsWorld ? static_cast<void*>(PhysicsWorld2D(m_PhysicsWorld)->World) : nullptr;
    }

}
