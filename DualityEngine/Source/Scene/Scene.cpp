#include "DualityEngine/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

#include <box2d/box2d.h>
#include <btBulletDynamicsCommon.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AudioImportSettings.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/SceneManager.h"
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

        void BeginContact(b2Contact* contact) override { Record(contact, true); }
        void EndContact(b2Contact* contact) override { Record(contact, false); }

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
    };
    static Physics2DWorld* PhysicsWorld2D(void* handle) { return static_cast<Physics2DWorld*>(handle); }

    // Bullet-backed 3D physics. Same "no pixels-per-meter conversion, gravity
    // scaled up to match this project's pixel-sized world units, +Y is down"
    // convention as the 2D physics above -- a Rigidbody3D dropped in a 3D
    // scene should fall the same visual direction as a Rigidbody2D would.
    static constexpr float DefaultGravityY3D = 400.0f;

    // Bullet, unlike Box2D, has no single "world" object -- it's a small
    // bundle of a collision configuration/dispatcher/broadphase/solver that
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
    };
    static Physics3DWorld* PhysicsWorld3D(void* handle) { return static_cast<Physics3DWorld*>(handle); }

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
    static btQuaternion EulerDegreesToBtQuaternion(const glm::vec3& rotationDegrees) {
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
    static glm::vec3 BtQuaternionToEulerDegrees(const btQuaternion& q) {
        glm::mat3 r = glm::mat3_cast(glm::quat(q.w(), q.x(), q.y(), q.z()));
        float y = std::asin(std::clamp(-r[0][2], -1.0f, 1.0f));
        float x = std::atan2(r[1][2], r[2][2]);
        float z = std::atan2(r[0][1], r[0][0]);
        return glm::degrees(glm::vec3(x, y, z));
    }

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
            AudioEngine::Play(path, loop, AudioImportSettings::Load(path).Volume);
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

    static void EngineServices_LogInfo(const char* message) { Log::Info(message); }
    static void EngineServices_LogWarn(const char* message) { Log::Warn(message); }
    static void EngineServices_LogError(const char* message) { Log::Error(message); }

    static void EngineServices_RequestLoadScene(const char* assetsRelativePath) {
        SceneManager::RequestLoadScene(assetsRelativePath);
    }

    static bool EngineServices_Instantiate(void* scenePtr, const char* prefabAssetGuid, unsigned int* outHandle) {
        std::string path = AssetDatabase::ResolvePath(prefabAssetGuid);
        if (path.empty())
            return false;
        Entity result = PrefabSerializer::Instantiate(*static_cast<Scene*>(scenePtr), path);
        if (!result)
            return false;
        *outHandle = static_cast<unsigned int>(result.Handle());
        return true;
    }

    static void* EngineServices_LoadScriptableObject(const char* assetGuid) {
        std::string path = AssetDatabase::ResolvePath(assetGuid);
        if (path.empty())
            return nullptr;
        return ScriptableObjectLoader::Load(path).Instance;
    }

    // Rigidbody velocity/force API (Unity's Rigidbody2D/Rigidbody) -- routed through
    // EngineServices for the same reason PlaySound/GetAxis are: b2Body/btRigidBody methods
    // aren't header-only (real compiled code in libbox2d.a/libBulletDynamics.a), so a
    // GameScripts.dll that doesn't link either library can't call them directly.
    // `entityHandle` is always the calling Behaviour's own GetEntity().Handle() in practice.
    // Registry() is Scene's own public accessor -- no new Scene method needed for this.
    // Returns false/no-op if the entity has no Rigidbody of that dimension, or Play isn't
    // running yet (RuntimeBody is null outside Play) -- same graceful-degradation convention
    // as every other EngineServices call.
    static bool EngineServices_GetVelocity2D(void* scenePtr, unsigned int entityHandle, float* outX, float* outY) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody2DComponent>(handle))
            return false;
        void* runtimeBody = registry.get<Rigidbody2DComponent>(handle).RuntimeBody;
        if (!runtimeBody)
            return false;
        const b2Vec2& v = static_cast<b2Body*>(runtimeBody)->GetLinearVelocity();
        *outX = v.x;
        *outY = v.y;
        return true;
    }

    static void EngineServices_SetVelocity2D(void* scenePtr, unsigned int entityHandle, float x, float y) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody2DComponent>(handle))
            return;
        void* runtimeBody = registry.get<Rigidbody2DComponent>(handle).RuntimeBody;
        if (runtimeBody)
            static_cast<b2Body*>(runtimeBody)->SetLinearVelocity(b2Vec2(x, y));
    }

    static void EngineServices_AddForce2D(void* scenePtr, unsigned int entityHandle, float x, float y) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody2DComponent>(handle))
            return;
        void* runtimeBody = registry.get<Rigidbody2DComponent>(handle).RuntimeBody;
        if (runtimeBody)
            static_cast<b2Body*>(runtimeBody)->ApplyForceToCenter(b2Vec2(x, y), true); // true = wake the body
    }

    static bool EngineServices_GetVelocity3D(void* scenePtr, unsigned int entityHandle, float* outX, float* outY, float* outZ) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody3DComponent>(handle))
            return false;
        void* runtimeBody = registry.get<Rigidbody3DComponent>(handle).RuntimeBody;
        if (!runtimeBody)
            return false;
        const btVector3& v = static_cast<btRigidBody*>(runtimeBody)->getLinearVelocity();
        *outX = v.x();
        *outY = v.y();
        *outZ = v.z();
        return true;
    }

    static void EngineServices_SetVelocity3D(void* scenePtr, unsigned int entityHandle, float x, float y, float z) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody3DComponent>(handle))
            return;
        void* runtimeBody = registry.get<Rigidbody3DComponent>(handle).RuntimeBody;
        if (runtimeBody) {
            auto* body = static_cast<btRigidBody*>(runtimeBody);
            body->activate(true); // a sleeping body would otherwise just ignore this
            body->setLinearVelocity(btVector3(x, y, z));
        }
    }

    static void EngineServices_AddForce3D(void* scenePtr, unsigned int entityHandle, float x, float y, float z) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody3DComponent>(handle))
            return;
        void* runtimeBody = registry.get<Rigidbody3DComponent>(handle).RuntimeBody;
        if (runtimeBody) {
            auto* body = static_cast<btRigidBody*>(runtimeBody);
            body->activate(true);
            body->applyCentralForce(btVector3(x, y, z));
        }
    }

    static int EngineServices_GetPointerScreen() {
        return static_cast<int>(Input::GetPointerScreen());
    }

    static bool EngineServices_Raycast2D(void* scenePtr, float originX, float originY, float dirX, float dirY, float maxDistance,
                                          unsigned int* outHandle, float* outPointX, float* outPointY, float* outNormalX, float* outNormalY, float* outDistance) {
        RaycastHit2D hit = static_cast<Scene*>(scenePtr)->Raycast2D({ originX, originY }, { dirX, dirY }, maxDistance);
        if (!hit)
            return false;
        *outHandle = static_cast<unsigned int>(hit.HitEntity.Handle());
        *outPointX = hit.Point.x; *outPointY = hit.Point.y;
        *outNormalX = hit.Normal.x; *outNormalY = hit.Normal.y;
        *outDistance = hit.Distance;
        return true;
    }

    static bool EngineServices_Raycast3D(void* scenePtr, float originX, float originY, float originZ, float dirX, float dirY, float dirZ, float maxDistance,
                                          unsigned int* outHandle, float* outPointX, float* outPointY, float* outPointZ,
                                          float* outNormalX, float* outNormalY, float* outNormalZ, float* outDistance) {
        RaycastHit3D hit = static_cast<Scene*>(scenePtr)->Raycast3D({ originX, originY, originZ }, { dirX, dirY, dirZ }, maxDistance);
        if (!hit)
            return false;
        *outHandle = static_cast<unsigned int>(hit.HitEntity.Handle());
        *outPointX = hit.Point.x; *outPointY = hit.Point.y; *outPointZ = hit.Point.z;
        *outNormalX = hit.Normal.x; *outNormalY = hit.Normal.y; *outNormalZ = hit.Normal.z;
        *outDistance = hit.Distance;
        return true;
    }

    static bool EngineServices_ScreenPointToRay3D(void* scenePtr, int screen, float screenX, float screenY,
                                                   float* outOriginX, float* outOriginY, float* outOriginZ,
                                                   float* outDirX, float* outDirY, float* outDirZ) {
        glm::vec3 origin, direction;
        if (!static_cast<Scene*>(scenePtr)->ScreenPointToRay3D(static_cast<Screen>(screen), { screenX, screenY }, origin, direction))
            return false;
        *outOriginX = origin.x; *outOriginY = origin.y; *outOriginZ = origin.z;
        *outDirX = direction.x; *outDirY = direction.y; *outDirZ = direction.z;
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
        &EngineServices_LogInfo,
        &EngineServices_LogWarn,
        &EngineServices_LogError,
        &EngineServices_RequestLoadScene,
        &EngineServices_Instantiate,
        &EngineServices_LoadScriptableObject,
        &EngineServices_GetVelocity2D,
        &EngineServices_SetVelocity2D,
        &EngineServices_AddForce2D,
        &EngineServices_GetVelocity3D,
        &EngineServices_SetVelocity3D,
        &EngineServices_AddForce3D,
        &EngineServices_GetPointerScreen,
        &EngineServices_Raycast2D,
        &EngineServices_Raycast3D,
        &EngineServices_ScreenPointToRay3D,
    };

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity(m_Registry.create(), this);
        entity.AddComponent<TransformComponent>();
        auto& nameComponent = entity.AddComponent<NameComponent>();
        nameComponent.Name = name.empty() ? "Entity" : name;
        entity.AddComponent<TagComponent>();
        entity.AddComponent<ActiveComponent>();
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

    bool Scene::IsEffectivelyActive(Entity entity) {
        Entity current = entity;
        while (current) {
            if (!current.GetComponent<ActiveComponent>().Active)
                return false;
            current = current.GetComponent<HierarchyComponent>().Parent;
        }
        return true;
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

    namespace {
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

    RaycastHit2D Scene::Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) {
        RaycastHit2D result;
        if (!m_PhysicsWorld)
            return result; // not Playing -- no b2World to query yet

        glm::vec2 dir = glm::normalize(direction);
        glm::vec2 to = origin + dir * maxDistance;

        ClosestRayCastCallback2D callback;
        PhysicsWorld2D(m_PhysicsWorld)->World->RayCast(&callback, b2Vec2(origin.x, origin.y), b2Vec2(to.x, to.y));
        if (!callback.Hit)
            return result;

        // Same uintptr_t -> uint32_t -> entt::entity narrowing as Box2DContactListener::Record
        // above -- same userData convention, just read here instead of at contact time.
        entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(callback.Fixture->GetBody()->GetUserData().pointer));
        if (!m_Registry.valid(handle))
            return result;

        result.HitEntity = Entity(handle, this);
        result.Point = { callback.Point.x, callback.Point.y };
        result.Normal = { callback.Normal.x, callback.Normal.y };
        result.Distance = glm::distance(origin, result.Point);
        return result;
    }

    RaycastHit3D Scene::Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
        RaycastHit3D result;
        if (!m_PhysicsWorld3D)
            return result; // not Playing -- no btDiscreteDynamicsWorld to query yet

        glm::vec3 dir = glm::normalize(direction);
        glm::vec3 to = origin + dir * maxDistance;
        btVector3 from(origin.x, origin.y, origin.z);
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
        result.Point = { callback.m_hitPointWorld.x(), callback.m_hitPointWorld.y(), callback.m_hitPointWorld.z() };
        result.Normal = { callback.m_hitNormalWorld.x(), callback.m_hitNormalWorld.y(), callback.m_hitNormalWorld.z() };
        result.Distance = glm::distance(origin, result.Point);
        return result;
    }

    bool Scene::ScreenPointToRay3D(Screen screen, const glm::vec2& screenPoint, glm::vec3& outOrigin, glm::vec3& outDirection) {
        Entity camera = GetPrimaryCamera(screen);
        if (!camera)
            return false;
        auto& cameraComponent = camera.GetComponent<CameraComponent>();
        if (cameraComponent.Projection != ProjectionType::Perspective)
            return false; // an Orthographic "ray" needs parallel-projection handling this doesn't attempt

        TransformComponent camTransform = GetWorldTransform(camera);
        float screenWidth = (screen == Screen::Top) ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
        float screenHeight = (screen == Screen::Top) ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);
        float aspect = screenWidth / screenHeight;

        // Same T*Rz*Ry*Rx rotation order OpenGLRenderer3D::ComposeWorldMtx/Citro3DRenderer's
        // own copy build this exact camera's view matrix with (see those files' own "must
        // match" comments -- this is a third, independent copy for the same reason: a raycast
        // has to agree with whatever's actually rendered, or a click would hit the wrong
        // thing). Only the rotation is needed (forward/right/up); origin is the camera's own
        // world translation directly, no matrix needed for that part.
        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, glm::radians(camTransform.Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        rot = glm::rotate(rot, glm::radians(camTransform.Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(camTransform.Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 forward = glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 right = glm::vec3(rot * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        glm::vec3 up = glm::vec3(rot * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));

        // Same NDC-then-basis-composition formula ScenePanel.cpp's own 3D pick-ray uses (Editor
        // Scene-view click-to-select) -- this is the runtime/gameplay equivalent, operating on
        // a real CameraComponent instead of the Editor's free-roam orbit camera.
        float ndcX = (2.0f * (screenPoint.x / screenWidth)) - 1.0f;
        float ndcY = 1.0f - (2.0f * (screenPoint.y / screenHeight));
        float tanHalfFov = std::tan(glm::radians(cameraComponent.FovDegrees) * 0.5f);

        outOrigin = camTransform.Translation;
        outDirection = glm::normalize(forward + right * (ndcX * tanHalfFov * aspect) + up * (ndcY * tanHalfFov));
        return true;
    }

    void Scene::OnRuntimeStart() {
        Physics2DWorld* world2D = new Physics2DWorld();
        world2D->World = new b2World(b2Vec2(0.0f, DefaultGravityY));
        world2D->Listener = new Box2DContactListener();
        world2D->World->SetContactListener(world2D->Listener);
        m_PhysicsWorld = world2D;
        b2World* world = world2D->World; // local alias, keeps the body-creation loop below unchanged

        auto bodyView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
        for (auto handle : bodyView) {
            auto& rb = bodyView.get<Rigidbody2DComponent>(handle);

            // An entity that isn't effectively active when Play starts gets no physics
            // body at all (RuntimeBody stays nullptr, which every other body-consuming
            // loop already null-checks) -- scope cut: toggling Active mid-Play does NOT
            // dynamically add/remove the body, only whether it existed at Play start.
            if (!IsEffectivelyActive(Entity(handle, this)))
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
            bodyDef.position.Set(worldTransform.Translation.x, worldTransform.Translation.y);
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

            if (m_Registry.all_of<BoxCollider2DComponent>(handle)) {
                auto& box = m_Registry.get<BoxCollider2DComponent>(handle);
                b2PolygonShape shape;
                shape.SetAsBox(box.Size.x, box.Size.y, b2Vec2(box.Offset.x, box.Offset.y), 0.0f);
                b2FixtureDef fixtureDef;
                fixtureDef.shape = &shape;
                fixtureDef.density = box.Density;
                fixtureDef.friction = box.Friction;
                fixtureDef.restitution = box.Restitution;
                fixtureDef.isSensor = box.IsTrigger;
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
                fixtureDef.isSensor = circle.IsTrigger;
                circle.RuntimeFixture = body->CreateFixture(&fixtureDef);
            }
        }

        Physics3DWorld* world3D = new Physics3DWorld();
        world3D->CollisionConfig = new btDefaultCollisionConfiguration();
        world3D->Dispatcher = new btCollisionDispatcher(world3D->CollisionConfig);
        world3D->Broadphase = new btDbvtBroadphase();
        world3D->Solver = new btSequentialImpulseConstraintSolver();
        world3D->World = new btDiscreteDynamicsWorld(world3D->Dispatcher, world3D->Broadphase, world3D->Solver, world3D->CollisionConfig);
        world3D->World->setGravity(btVector3(0.0f, DefaultGravityY3D, 0.0f));
        m_PhysicsWorld3D = world3D;

        auto body3DView = m_Registry.view<Rigidbody3DComponent, TransformComponent>();
        for (auto handle : body3DView) {
            auto& rb = body3DView.get<Rigidbody3DComponent>(handle);

            // Same "no body at all if not active at Play start" rule as the 2D loop above.
            if (!IsEffectivelyActive(Entity(handle, this)))
                continue;

            // Same identity-parent-only limitation as the 2D loop above.
            TransformComponent worldTransform = GetWorldTransform(Entity(handle, this));

            btCollisionShape* baseShape = nullptr; // the box/sphere itself, before any offset wrapping
            glm::vec3 offset{ 0.0f, 0.0f, 0.0f };
            float density = 1.0f, friction = 0.5f, restitution = 0.0f;
            float volume = 0.0f; // 0 = no collider, handled below
            bool isTrigger = false;

            if (m_Registry.all_of<BoxCollider3DComponent>(handle)) {
                auto& box = m_Registry.get<BoxCollider3DComponent>(handle);
                baseShape = new btBoxShape(btVector3(box.Size.x, box.Size.y, box.Size.z));
                offset = box.Offset;
                density = box.Density; friction = box.Friction; restitution = box.Restitution;
                volume = (2.0f * box.Size.x) * (2.0f * box.Size.y) * (2.0f * box.Size.z);
                isTrigger = box.IsTrigger;
            } else if (m_Registry.all_of<SphereCollider3DComponent>(handle)) {
                auto& sphere = m_Registry.get<SphereCollider3DComponent>(handle);
                baseShape = new btSphereShape(sphere.Radius);
                offset = sphere.Offset;
                density = sphere.Density; friction = sphere.Friction; restitution = sphere.Restitution;
                volume = (4.0f / 3.0f) * glm::pi<float>() * sphere.Radius * sphere.Radius * sphere.Radius;
                isTrigger = sphere.IsTrigger;
            } else {
                // A Rigidbody3D with no collider still gets a real body (matching the
                // 2D loop's own zero-fixture case above) -- btEmptyShape is Bullet's
                // supported "no collision volume" shape, used here purely so the body
                // has a valid, deletable btCollisionShape to construct with.
                baseShape = new btEmptyShape();
            }

            btCollisionShape* attachedShape = baseShape;
            if (offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f) {
                btCompoundShape* compound = new btCompoundShape();
                btTransform localTransform; localTransform.setIdentity();
                localTransform.setOrigin(btVector3(offset.x, offset.y, offset.z));
                compound->addChildShape(localTransform, baseShape);
                attachedShape = compound;
            }
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
            startTransform.setOrigin(btVector3(worldTransform.Translation.x, worldTransform.Translation.y, worldTransform.Translation.z));
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

        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            auto& bc = behaviourView.get<BehaviourComponent>(handle);
            if (bc.Instance || bc.ClassName.empty())
                continue;

            if (ScriptRegistry::TryCreate(bc.ClassName, &bc.Instance, &bc.Destroy)) {
                bc.Instance->m_Entity = Entity(handle, this);
                bc.Instance->SetEngineServices(&s_EngineServices);

                // Apply Edit-mode Inspector overrides (see BehaviourComponent's own comment)
                // onto the fresh instance's real fields before OnCreate() sees them --
                // matches Unity's own field-initialization-before-Awake ordering.
                const std::vector<FieldHandle>& fields = ScriptRegistry::GetFields(bc.ClassName);
                for (auto& [name, value] : bc.PropertyOverrides) {
                    for (auto& field : fields) {
                        if (field.Name == name) {
                            field.Set(bc.Instance, value);
                            break;
                        }
                    }
                }

                bc.Instance->OnCreate();
            } else {
                Log::Error("Behaviour: unknown script class '" + bc.ClassName + "'");
            }
        }
    }

    void Scene::OnRuntimeUpdate(float deltaTime) {
        // Filled by Box2DContactListener (repointed here for the duration of this one
        // Step call) and/or the Bullet manifold-diff below, then dispatched to
        // Behaviour::OnCollisionEnter/Exit/OnTriggerEnter/Exit right after both physics
        // steps -- see PhysicsContactEvent's own comment.
        std::vector<PhysicsContactEvent> contactEvents;

        if (m_PhysicsWorld) {
            Physics2DWorld* world2D = PhysicsWorld2D(m_PhysicsWorld);
            world2D->Listener->Events = &contactEvents;

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
                static_cast<b2Body*>(rb.RuntimeBody)->SetTransform(b2Vec2(transform.Translation.x, transform.Translation.y), glm::radians(transform.Rotation.z));
            }

            world2D->World->Step(deltaTime, VelocityIterations, PositionIterations);

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

        if (m_PhysicsWorld3D) {
            Physics3DWorld* world3D = PhysicsWorld3D(m_PhysicsWorld3D);

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
                kinematicTransform.setOrigin(btVector3(transform.Translation.x, transform.Translation.y, transform.Translation.z));
                kinematicTransform.setRotation(EulerDegreesToBtQuaternion(transform.Rotation));
                static_cast<btRigidBody*>(rb.RuntimeBody)->getMotionState()->setWorldTransform(kinematicTransform);
            }

            world3D->World->stepSimulation(deltaTime);

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
                transform.Translation.x = position.x();
                transform.Translation.y = position.y();
                transform.Translation.z = position.z();
                transform.Rotation = BtQuaternionToEulerDegrees(worldTransform.getRotation());
            }

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
                return false;
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

        // Dispatch collision/trigger events to both sides of each pair (Unity's own
        // convention -- each side's script, if any, gets called with the OTHER entity).
        // m_Registry.valid() guards against an entity destroyed by an earlier event's own
        // handler this same frame.
        for (auto& event : contactEvents) {
            if (!m_Registry.valid(event.A) || !m_Registry.valid(event.B))
                continue;
            auto fire = [this](entt::entity self, Entity other, bool isTrigger, bool isBegin) {
                if (!m_Registry.all_of<BehaviourComponent>(self))
                    return;
                auto& bc = m_Registry.get<BehaviourComponent>(self);
                if (!bc.Instance)
                    return;
                if (isTrigger) {
                    if (isBegin) bc.Instance->OnTriggerEnter(other); else bc.Instance->OnTriggerExit(other);
                } else {
                    if (isBegin) bc.Instance->OnCollisionEnter(other); else bc.Instance->OnCollisionExit(other);
                }
            };
            fire(event.A, Entity(event.B, this), event.IsTrigger, event.IsBegin);
            fire(event.B, Entity(event.A, this), event.IsTrigger, event.IsBegin);
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
            if (!bc.Instance)
                continue;

            bool active = IsEffectivelyActive(Entity(handle, this));
            if (active != bc.WasActiveLastFrame) {
                if (active)
                    bc.Instance->OnEnable();
                else
                    bc.Instance->OnDisable();
                bc.WasActiveLastFrame = active;
            }
            if (active)
                bc.Instance->OnUpdate(deltaTime);
        }
    }

    void Scene::OnRuntimeStop() {
        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            auto& bc = behaviourView.get<BehaviourComponent>(handle);
            if (bc.Instance) {
                if (bc.WasActiveLastFrame)
                    bc.Instance->OnDisable();
                bc.Instance->OnDestroy();
                bc.Destroy(bc.Instance);
                bc.Instance = nullptr;
            }
        }

        if (m_PhysicsWorld) {
            Physics2DWorld* world2D = PhysicsWorld2D(m_PhysicsWorld);
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
        }

        if (m_PhysicsWorld3D) {
            Physics3DWorld* world3D = PhysicsWorld3D(m_PhysicsWorld3D);

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
                    if (shape->isCompound()) {
                        // Only ever one child -- see the Offset-wrapping comment in
                        // OnRuntimeStart -- but loop for correctness regardless.
                        btCompoundShape* compound = static_cast<btCompoundShape*>(shape);
                        for (int i = 0; i < compound->getNumChildShapes(); i++)
                            delete compound->getChildShape(i);
                    }
                    delete shape;
                    rb.RuntimeCollisionShape = nullptr;
                }
            }

            delete world3D->World;
            delete world3D->Solver;
            delete world3D->Broadphase;
            delete world3D->Dispatcher;
            delete world3D->CollisionConfig;
            delete world3D;
            m_PhysicsWorld3D = nullptr;
        }
    }

    void Scene::Clear() {
        m_Registry.clear();
        m_RootEntities.clear();
    }

}
