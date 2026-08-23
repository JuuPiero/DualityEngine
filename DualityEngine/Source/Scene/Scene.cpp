#include "DualityEngine/Scene/Scene.h"

#include <box2d/box2d.h>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/EngineServices.h"
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

    // Frames are a contiguous prefix -- the first empty slot ends the
    // sequence, matching how a user naturally fills Frame0, Frame1, ...
    // in order rather than leaving gaps.
    static int FlipbookFrameCount(SpriteFlipbookComponent& flipbook) {
        for (int i = 0; i < 8; i++) {
            if (GetFlipbookFrame(flipbook, i).Guid.empty())
                return i;
        }
        return 8;
    }

    // Adapters from EngineServices' plain-C-type ABI signatures to
    // Duality::Input's real ones -- these and Input itself are both
    // compiled into this same DualityEngine library, so this is an
    // ordinary same-binary call, not a boundary crossing. Handed to every
    // Behaviour instance right after it's created (see OnRuntimeStart
    // below) since a Behaviour may live inside a separately-compiled
    // GameScripts.dll on desktop that can't call Duality::Input directly.
    static bool EngineServices_GetKey(int keyCode) { return Input::GetKey(static_cast<KeyCode>(keyCode)); }
    static bool EngineServices_GetKeyDown(int keyCode) { return Input::GetKeyDown(static_cast<KeyCode>(keyCode)); }
    static bool EngineServices_GetKeyUp(int keyCode) { return Input::GetKeyUp(static_cast<KeyCode>(keyCode)); }
    static float EngineServices_GetAxis(const char* axisName) { return Input::GetAxis(axisName); }
    static bool EngineServices_GetPointerDown() { return Input::GetPointerDown(); }
    static void EngineServices_GetPointerPosition(float* outX, float* outY) {
        glm::vec2 position = Input::GetPointerPosition();
        *outX = position.x;
        *outY = position.y;
    }
    static void EngineServices_PlaySound(const char* assetGuid, bool loop) {
        std::string path = AssetDatabase::ResolvePath(assetGuid);
        if (!path.empty())
            AudioEngine::Play(path, loop);
    }
    static void EngineServices_StopAllSounds() { AudioEngine::StopAll(); }

    static const EngineServices s_EngineServices = {
        &EngineServices_GetKey,
        &EngineServices_GetKeyDown,
        &EngineServices_GetKeyUp,
        &EngineServices_GetAxis,
        &EngineServices_GetPointerDown,
        &EngineServices_GetPointerPosition,
        &EngineServices_PlaySound,
        &EngineServices_StopAllSounds,
    };

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
            // TransformComponent::Rotation is always in degrees (matching
            // Unity and the Properties panel's plain drag-float) -- Box2D's
            // own angle is radians, so the boundary conversion happens here.
            bodyDef.angle = glm::radians(transform.Rotation.z);
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
                bc.Instance->SetEngineServices(&s_EngineServices);
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
                transform.Rotation.z = glm::degrees(body->GetAngle());
            }
        }

        auto flipbookView = m_Registry.view<SpriteFlipbookComponent>();
        for (auto handle : flipbookView) {
            auto& flipbook = flipbookView.get<SpriteFlipbookComponent>(handle);
            if (!flipbook.Playing)
                continue;

            int frameCount = FlipbookFrameCount(flipbook);
            if (frameCount <= 1)
                continue; // nothing to animate

            flipbook.ElapsedTime += deltaTime;
            while (flipbook.FrameDuration > 0.0f && flipbook.ElapsedTime >= flipbook.FrameDuration) {
                flipbook.ElapsedTime -= flipbook.FrameDuration;
                flipbook.CurrentFrame++;
                if (flipbook.CurrentFrame >= frameCount) {
                    if (flipbook.Loop) {
                        flipbook.CurrentFrame = 0;
                    } else {
                        flipbook.CurrentFrame = frameCount - 1;
                        flipbook.Playing = false;
                        break;
                    }
                }
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
