#include "DualityEngine/Scene/Scene.h"

#include <box2d/box2d.h>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    // No pixels-per-meter conversion -- Transform units are fed to Box2D
    // directly (matching how this project's reference prototype does it).
    // Box2D's default tuning assumes roughly 0.1-10 unit sized objects, so
    // gravity is scaled up from the usual 9.8 to stay visually reasonable
    // against this project's ~16-80 unit sprite sizes. Positive, not
    // negative: screen/world Y increases downward here (matches the
    // renderer's pixel-space convention), so "falling" means increasing Y.
    static constexpr float DefaultGravityY = 400.0f;
    static constexpr int32 VelocityIterations = 6;
    static constexpr int32 PositionIterations = 2;

    static b2World* PhysicsWorld(void* handle) { return static_cast<b2World*>(handle); }

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity(m_Registry.create(), this);
        entity.AddComponent<TransformComponent>();
        auto& nameComponent = entity.AddComponent<NameComponent>();
        nameComponent.Name = name.empty() ? "Entity" : name;
        entity.AddComponent<TagComponent>();
        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        m_Registry.destroy(entity.Handle());
    }

    Entity Scene::GetPrimaryCamera(Screen screen) {
        auto view = m_Registry.view<CameraComponent>();
        for (auto handle : view) {
            const auto& camera = view.get<CameraComponent>(handle);
            if (camera.Screen == screen && camera.Primary)
                return Entity(handle, this);
        }
        return Entity{};
    }

    void Scene::OnRuntimeStart() {
        b2World* world = new b2World(b2Vec2(0.0f, DefaultGravityY));
        m_PhysicsWorld = world;

        auto bodyView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
        for (auto handle : bodyView) {
            auto& rb = bodyView.get<Rigidbody2DComponent>(handle);
            auto& transform = bodyView.get<TransformComponent>(handle);

            b2BodyDef bodyDef;
            bodyDef.type = rb.IsStatic ? b2_staticBody : b2_dynamicBody;
            bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
            bodyDef.angle = transform.Rotation.z;
            bodyDef.fixedRotation = rb.FixedRotation;
            b2Body* body = world->CreateBody(&bodyDef);
            rb.RuntimeBody = body;

            if (m_Registry.all_of<BoxCollider2DComponent>(handle)) {
                auto& box = m_Registry.get<BoxCollider2DComponent>(handle);
                b2PolygonShape shape;
                shape.SetAsBox(box.Size.x, box.Size.y, b2Vec2(box.Offset.x, box.Offset.y), 0.0f);
                b2FixtureDef fixtureDef;
                fixtureDef.shape = &shape;
                fixtureDef.density = box.Density;
                fixtureDef.friction = box.Friction;
                fixtureDef.restitution = box.Restitution;
                box.RuntimeFixture = body->CreateFixture(&fixtureDef);
            }

            if (m_Registry.all_of<CircleCollider2DComponent>(handle)) {
                auto& circle = m_Registry.get<CircleCollider2DComponent>(handle);
                b2CircleShape shape;
                shape.m_p.Set(circle.Offset.x, circle.Offset.y);
                shape.m_radius = circle.Radius;
                b2FixtureDef fixtureDef;
                fixtureDef.shape = &shape;
                fixtureDef.density = circle.Density;
                fixtureDef.friction = circle.Friction;
                fixtureDef.restitution = circle.Restitution;
                circle.RuntimeFixture = body->CreateFixture(&fixtureDef);
            }
        }

        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            auto& bc = behaviourView.get<BehaviourComponent>(handle);
            if (bc.Instance || bc.ClassName.empty())
                continue;

            if (ScriptRegistry::TryCreate(bc.ClassName, &bc.Instance, &bc.Destroy)) {
                bc.Instance->m_Entity = Entity(handle, this);
                bc.Instance->OnCreate();
            } else {
                Log::Error("Behaviour: unknown script class '" + bc.ClassName + "'");
            }
        }
    }

    void Scene::OnRuntimeUpdate(float deltaTime) {
        if (m_PhysicsWorld) {
            PhysicsWorld(m_PhysicsWorld)->Step(deltaTime, VelocityIterations, PositionIterations);

            auto bodyView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
            for (auto handle : bodyView) {
                auto& rb = bodyView.get<Rigidbody2DComponent>(handle);
                auto& transform = bodyView.get<TransformComponent>(handle);
                if (!rb.RuntimeBody)
                    continue;
                b2Body* body = static_cast<b2Body*>(rb.RuntimeBody);
                const b2Vec2& position = body->GetPosition();
                transform.Translation.x = position.x;
                transform.Translation.y = position.y;
                transform.Rotation.z = body->GetAngle();
            }
        }

        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            auto& bc = behaviourView.get<BehaviourComponent>(handle);
            if (bc.Instance)
                bc.Instance->OnUpdate(deltaTime);
        }
    }

    void Scene::OnRuntimeStop() {
        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            auto& bc = behaviourView.get<BehaviourComponent>(handle);
            if (bc.Instance) {
                bc.Instance->OnDestroy();
                bc.Destroy(bc.Instance);
                bc.Instance = nullptr;
            }
        }

        if (m_PhysicsWorld) {
            delete PhysicsWorld(m_PhysicsWorld); // also frees every body/fixture it owns
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
        }
    }

}
