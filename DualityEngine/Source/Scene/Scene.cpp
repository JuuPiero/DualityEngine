#include "DualityEngine/Scene/Scene.h"

#include <algorithm>
#include <cmath>

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

    // 2D affine compose: rotates+scales `local` (already itself a
    // TransformComponent-shaped translate/rotate.z/scale) by `parentWorld`, then
    // offsets by parentWorld's own translation. Shared by GetWorldTransform (walking
    // down from the root) and, via its inverse below, by SetParent's
    // world-position-preserving reparent.
    static TransformComponent ComposeWorld(const TransformComponent& local, const TransformComponent& parentWorld) {
        float parentRad = glm::radians(parentWorld.Rotation.z);
        float cosR = std::cos(parentRad), sinR = std::sin(parentRad);
        glm::vec2 scaledLocal{ local.Translation.x * parentWorld.Scale.x, local.Translation.y * parentWorld.Scale.y };
        glm::vec2 rotatedLocal{ scaledLocal.x * cosR - scaledLocal.y * sinR, scaledLocal.x * sinR + scaledLocal.y * cosR };

        TransformComponent world;
        world.Translation = {
            parentWorld.Translation.x + rotatedLocal.x,
            parentWorld.Translation.y + rotatedLocal.y,
            parentWorld.Translation.z + local.Translation.z
        };
        world.Rotation = parentWorld.Rotation + local.Rotation;
        world.Scale = parentWorld.Scale * local.Scale;
        return world;
    }

    // Exact inverse of ComposeWorld -- given an entity's desired world transform and
    // its (new) parent's world transform, returns the local transform that would
    // compose back to it. Used only by SetParent's preserveWorldPosition path.
    static TransformComponent DecomposeWorld(const TransformComponent& world, const TransformComponent& parentWorld) {
        auto safeDiv = [](float a, float b) { return std::abs(b) > 1e-6f ? a / b : a; };

        float parentRad = glm::radians(parentWorld.Rotation.z);
        float cosR = std::cos(-parentRad), sinR = std::sin(-parentRad);
        glm::vec2 offset{ world.Translation.x - parentWorld.Translation.x, world.Translation.y - parentWorld.Translation.y };
        glm::vec2 unrotated{ offset.x * cosR - offset.y * sinR, offset.x * sinR + offset.y * cosR };

        TransformComponent local;
        local.Translation = {
            safeDiv(unrotated.x, parentWorld.Scale.x),
            safeDiv(unrotated.y, parentWorld.Scale.y),
            world.Translation.z - parentWorld.Translation.z
        };
        local.Rotation = world.Rotation - parentWorld.Rotation;
        local.Scale = {
            safeDiv(world.Scale.x, parentWorld.Scale.x),
            safeDiv(world.Scale.y, parentWorld.Scale.y),
            safeDiv(world.Scale.z, parentWorld.Scale.z)
        };
        return local;
    }

    // Walks up from `entity` to the root, true if `ancestor` is somewhere in that
    // chain -- used by SetParent to refuse a reparent that would create a cycle.
    static bool IsDescendantOf(Entity entity, Entity ancestor) {
        Entity current = entity;
        while (current) {
            if (current == ancestor)
                return true;
            current = current.GetComponent<HierarchyComponent>().Parent;
        }
        return false;
    }

    // Depth-first search of `root`'s own subtree (root included) for an entity named
    // `name` -- used by FindEntityInScreen to search within a ScreenGroupComponent's
    // group. Mirrors IsDescendantOf's style of walking HierarchyComponent by hand.
    static Entity FindByNameInSubtree(Entity root, const std::string& name) {
        if (root.GetComponent<NameComponent>().Name == name)
            return root;
        for (Entity child : root.GetComponent<HierarchyComponent>().Children) {
            Entity found = FindByNameInSubtree(child, name);
            if (found)
                return found;
        }
        return Entity{};
    }

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

    // `scenePtr` travels per-call (not baked into s_EngineServices below, which is one
    // shared static instance) since, unlike Input/Audio, entity lookup is inherently
    // per-Scene -- Behaviour supplies its own GetEntity().GetScene() each call.
    static bool EngineServices_FindEntityInScreen(void* scenePtr, int screen, const char* name, unsigned int* outHandle) {
        Entity found = static_cast<Scene*>(scenePtr)->FindEntityInScreen(static_cast<Screen>(screen), name);
        if (!found)
            return false;
        *outHandle = static_cast<unsigned int>(found.Handle());
        return true;
    }

    static const EngineServices s_EngineServices = {
        &EngineServices_GetKey,
        &EngineServices_GetKeyDown,
        &EngineServices_GetKeyUp,
        &EngineServices_GetAxis,
        &EngineServices_GetPointerDown,
        &EngineServices_GetPointerPosition,
        &EngineServices_PlaySound,
        &EngineServices_StopAllSounds,
        &EngineServices_FindEntityInScreen,
    };

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity(m_Registry.create(), this);
        entity.AddComponent<TransformComponent>();
        auto& nameComponent = entity.AddComponent<NameComponent>();
        nameComponent.Name = name.empty() ? "Entity" : name;
        entity.AddComponent<TagComponent>();
        entity.AddComponent<HierarchyComponent>();
        m_RootEntities.push_back(entity);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        auto& hierarchy = entity.GetComponent<HierarchyComponent>();

        // Copy Children before recursing -- destroying a child never mutates its
        // siblings' list (only detaching from a *parent* does, below), but it's
        // cheap insurance against iterating a vector while it's being torn down.
        std::vector<Entity> children = hierarchy.Children;
        for (Entity child : children)
            DestroyEntity(child);

        std::vector<Entity>& siblings = SiblingListFor(hierarchy.Parent);
        siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());

        m_Registry.destroy(entity.Handle());
    }

    std::vector<Entity>& Scene::SiblingListFor(Entity parent) {
        if (!parent)
            return m_RootEntities;
        return parent.GetComponent<HierarchyComponent>().Children;
    }

    void Scene::SetParent(Entity child, Entity newParent, Entity insertAfter, bool preserveWorldPosition) {
        if (!child || child == newParent)
            return;
        if (newParent && IsDescendantOf(newParent, child))
            return; // would create a cycle

        auto& childHierarchy = child.GetComponent<HierarchyComponent>();
        Entity oldParent = childHierarchy.Parent;

        // Captured before any mutation -- ComposeWorld/DecomposeWorld below need
        // child's world transform under its *old* parent chain.
        TransformComponent worldBefore = GetWorldTransform(child);

        std::vector<Entity>& oldSiblings = SiblingListFor(oldParent);
        oldSiblings.erase(std::remove(oldSiblings.begin(), oldSiblings.end(), child), oldSiblings.end());

        std::vector<Entity>& newSiblings = SiblingListFor(newParent);
        auto insertPos = newSiblings.end();
        if (insertAfter) {
            auto it = std::find(newSiblings.begin(), newSiblings.end(), insertAfter);
            if (it != newSiblings.end())
                insertPos = it + 1;
        }
        newSiblings.insert(insertPos, child);

        childHierarchy.Parent = newParent;

        if (preserveWorldPosition) {
            TransformComponent parentWorld = newParent ? GetWorldTransform(newParent) : TransformComponent{};
            child.GetComponent<TransformComponent>() = DecomposeWorld(worldBefore, parentWorld);
        }
    }

    TransformComponent Scene::GetWorldTransform(Entity entity) {
        TransformComponent local = entity.GetComponent<TransformComponent>();
        Entity parent = entity.GetComponent<HierarchyComponent>().Parent;
        if (!parent)
            return local;
        return ComposeWorld(local, GetWorldTransform(parent));
    }

    Entity Scene::FindEntityInScreen(Screen screen, const std::string& name) {
        bool anyGroupForScreen = false;
        for (auto handle : m_Registry.view<ScreenGroupComponent>()) {
            auto& group = m_Registry.get<ScreenGroupComponent>(handle);
            if (group.Screen != screen)
                continue;
            anyGroupForScreen = true;
            Entity found = FindByNameInSubtree(Entity(handle, this), name);
            if (found)
                return found;
        }
        if (anyGroupForScreen)
            return Entity{}; // groups exist for this screen, but `name` wasn't in any of them

        // No ScreenGroupComponent adopted for this screen yet -- fall back to a
        // scene-wide by-name search so the API isn't a no-op out of the box.
        for (auto handle : m_Registry.view<NameComponent>()) {
            if (m_Registry.get<NameComponent>(handle).Name == name)
                return Entity(handle, this);
        }
        return Entity{};
    }

    bool Scene::TryResolveEntityScreen(Entity entity, Screen& outScreen) {
        Entity current = entity;
        while (current) {
            if (current.HasComponent<ScreenGroupComponent>()) {
                outScreen = current.GetComponent<ScreenGroupComponent>().Screen;
                return true;
            }
            if (current.HasComponent<CameraComponent>()) {
                outScreen = current.GetComponent<CameraComponent>().Screen;
                return true;
            }
            current = current.GetComponent<HierarchyComponent>().Parent;
        }
        return false;
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
            bodyDef.type = rb.IsStatic ? b2_staticBody : b2_dynamicBody;
            bodyDef.position.Set(worldTransform.Translation.x, worldTransform.Translation.y);
            // TransformComponent::Rotation is always in degrees (matching
            // Unity and the Properties panel's plain drag-float) -- Box2D's
            // own angle is radians, so the boundary conversion happens here.
            bodyDef.angle = glm::radians(worldTransform.Rotation.z);
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
                // Box2D always simulates in world space and this writes straight into
                // the entity's own (local) TransformComponent -- correct only if this
                // entity has no parent, or its parent stays at identity. See the
                // matching comment at body-creation time in OnRuntimeStart.
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
