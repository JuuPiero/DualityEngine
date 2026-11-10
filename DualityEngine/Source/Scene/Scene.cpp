#include "DualityEngine/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
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
#include "DualityEngine/Asset/PhysicsMaterialLoader.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Scene/PhysicsSystems.h"
#include "Systems/PhysicsWorlds.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Core/Profiler.h"
#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PhysicsRaycaster.h"
#include "DualityEngine/Scene/PointerEventHandlers.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scripting/EngineServices.h"
#include "DualityEngine/Scripting/ScriptContext.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"
#include "DualityEngine/Scripting/JS/JsScripting.h"

namespace Duality {

    // One active runtime Scene exists in each player/editor session. These
    // values are read by the Time scripting API through EngineServices, so
    // GameScripts.dll never relies on duplicate header-only mutable state.
    static float s_RuntimeDeltaTime = 0.0f;
    static float s_RuntimeElapsedTime = 0.0f;
    static unsigned long long s_RuntimeFrameCount = 0;

    // 2D affine compose: rotates+scales `local` (already itself a
    // TransformComponent-shaped translate/rotate.z/scale) by `parentWorld`, then
    // offsets by parentWorld's own translation. Shared by GetWorldTransform (walking
    // down from the root) and, via its inverse below, by SetParent's
    // world-position-preserving reparent.
    // True when the 2D formula below is exact: no X/Y rotation anywhere and no Z offset that a
    // parent Z scale would have to scale. Every 2D scene takes that path bit-for-bit as before.
    static bool IsPlanarCompose(const TransformComponent& local, const TransformComponent& parentWorld) {
        return local.Rotation.x == 0.0f && local.Rotation.y == 0.0f &&
            parentWorld.Rotation.x == 0.0f && parentWorld.Rotation.y == 0.0f &&
            (parentWorld.Scale.z == 1.0f || local.Translation.z == 0.0f);
    }

    // Full 3D TRS composition (imported model hierarchies, skeletons): rotation as
    // quaternions in the renderers' Rz*Ry*Rx order (glm::quat(euler) / glm::eulerAngles),
    // translation rotated by the parent's whole rotation, scale composed like Unity's lossyScale.
    static TransformComponent ComposeWorld3D(const TransformComponent& local, const TransformComponent& parentWorld) {
        const glm::quat parentRotation(glm::radians(parentWorld.Rotation));
        const glm::quat localRotation(glm::radians(local.Rotation));
        TransformComponent world;
        world.Translation = parentWorld.Translation + parentRotation * (parentWorld.Scale * local.Translation);
        world.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(parentRotation * localRotation)));
        world.Scale = parentWorld.Scale * local.Scale;
        return world;
    }

    static TransformComponent ComposeWorld(const TransformComponent& local, const TransformComponent& parentWorld) {
        if (!IsPlanarCompose(local, parentWorld))
            return ComposeWorld3D(local, parentWorld);
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

        const bool planar = world.Rotation.x == 0.0f && world.Rotation.y == 0.0f &&
            parentWorld.Rotation.x == 0.0f && parentWorld.Rotation.y == 0.0f &&
            (parentWorld.Scale.z == 1.0f || world.Translation.z == parentWorld.Translation.z);
        if (!planar) {
            // Inverse of ComposeWorld3D.
            const glm::quat inverseParent = glm::inverse(glm::quat(glm::radians(parentWorld.Rotation)));
            const glm::vec3 unrotated = inverseParent * (world.Translation - parentWorld.Translation);
            TransformComponent local;
            local.Translation = { safeDiv(unrotated.x, parentWorld.Scale.x), safeDiv(unrotated.y, parentWorld.Scale.y), safeDiv(unrotated.z, parentWorld.Scale.z) };
            local.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(inverseParent * glm::quat(glm::radians(world.Rotation)))));
            local.Scale = { safeDiv(world.Scale.x, parentWorld.Scale.x), safeDiv(world.Scale.y, parentWorld.Scale.y), safeDiv(world.Scale.z, parentWorld.Scale.z) };
            return local;
        }

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
    // `name` -- used by FindEntityInScreen to search within a LayerComponent-tagged
    // organizational root. Mirrors IsDescendantOf's style of walking HierarchyComponent by hand.
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

    static void PlayAudioSourceOnEntity(Scene* scene, Entity entity) {
        if (!entity.HasComponent<AudioSourceComponent>())
            return;
        auto& src = entity.GetComponent<AudioSourceComponent>();
        if (src.Mute || src.Clip.Guid.empty())
            return;
        std::string path = AssetDatabase::ResolvePath(src.Clip.Guid);
        if (path.empty())
            return;
        if (src.RuntimeHandle != InvalidAudioHandle)
            AudioEngine::Stop(src.RuntimeHandle);
        float importVolume = AudioImportSettings::Load(path).Volume;
        src.RuntimeHandle = AudioEngine::Play(path, src.Loop, importVolume * src.Volume);
        src.Paused = false;
    }

    static Entity EntityFromSceneHandle(Scene* scene, unsigned int entityHandle) {
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!scene->Registry().valid(handle))
            return Entity{};
        return Entity(handle, scene);
    }

    // Adapters from EngineServices' plain-C-type ABI signatures to
    // Duality::Input's real ones -- these and Input itself are both
    // compiled into this same DualityEngine library, so this is an
    // ordinary same-binary call, not a boundary crossing. Handed to every
    // Behaviour instance right after it's created (see OnRuntimeStart
    // below) since a Behaviour may live inside a separately-compiled
    // GameScripts.dll on desktop that can't call Duality::Input directly.
    static bool EngineServices_GetKey(int keyCode) { return InputManager::GetKey(static_cast<KeyCode>(keyCode)); }
    static bool EngineServices_GetKeyDown(int keyCode) { return InputManager::GetKeyDown(static_cast<KeyCode>(keyCode)); }
    static bool EngineServices_GetKeyUp(int keyCode) { return InputManager::GetKeyUp(static_cast<KeyCode>(keyCode)); }
    static float EngineServices_GetAxis(const char* axisName) { return InputManager::GetAxis(axisName); }
    static bool EngineServices_GetPointerDown() { return InputManager::GetPointerDown(); }
    static void EngineServices_GetPointerPosition(float* outX, float* outY) {
        glm::vec2 position = InputManager::GetPointerPosition();
        *outX = position.x;
        *outY = position.y;
    }
    static float EngineServices_GetDeltaTime() { return s_RuntimeDeltaTime; }
    static float EngineServices_GetElapsedTime() { return s_RuntimeElapsedTime; }
    static unsigned long long EngineServices_GetFrameCount() { return s_RuntimeFrameCount; }
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

    static void EngineServices_RequestLoadScene(const char* assetsRelativePath, int mode) {
        SceneManager::EnqueueLoadRequest(assetsRelativePath ? assetsRelativePath : "", mode == static_cast<int>(LoadSceneMode::Additive) ? LoadSceneMode::Additive : LoadSceneMode::Single);
    }
    static void EngineServices_RequestUnloadScene(const char* assetsRelativePath) {
        SceneManager::EnqueueUnloadRequest(assetsRelativePath ? assetsRelativePath : "");
    }

    static bool EngineServices_Instantiate(void* scenePtr, const char* prefabAssetGuid, unsigned int* outHandle) {
        std::string path = AssetDatabase::ResolvePath(prefabAssetGuid);
        if (path.empty())
            return false;
        Entity result = PrefabSerializer::Instantiate(*static_cast<Scene*>(scenePtr), path, {}, AssetRef{ prefabAssetGuid });
        if (!result)
            return false;
        // During Play the spawned hierarchy's scripts (C++ and TypeScript) are created and
        // Awaked right away, like Unity's Instantiate.
        static_cast<Scene*>(scenePtr)->CreateRuntimeScripts(result);
        *outHandle = static_cast<unsigned int>(result.Handle());
        return true;
    }

    static void EngineServices_DestroyEntity(void* scenePtr, unsigned int entityHandle) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        Entity entity = EntityFromSceneHandle(scene, entityHandle);
        if (entity)
            scene->DestroyEntityDeferred(entity);
    }

    static void* EngineServices_FindScript(void* scenePtr, unsigned int entityHandle, const char* className) {
        Entity entity = EntityFromSceneHandle(static_cast<Scene*>(scenePtr), entityHandle);
        if (!entity || !className || !entity.HasComponent<BehaviourComponent>())
            return nullptr;
        for (auto& slot : entity.GetComponent<BehaviourComponent>().Scripts)
            if (slot.Instance && slot.ClassName == className)
                return slot.Instance;
        return nullptr;
    }

    static const FieldHandle* FindScriptField(const char* className, const char* fieldName) {
        if (!className || !fieldName)
            return nullptr;
        for (const FieldHandle& field : ScriptRegistry::GetFields(className))
            if (field.Name == fieldName)
                return &field;
        return nullptr;
    }

    static bool EngineServices_GetScriptField(void* script, const char* className, const char* fieldName, void* outValue) {
        const FieldHandle* field = FindScriptField(className, fieldName);
        if (!script || !field || !outValue)
            return false;
        *static_cast<FieldValue*>(outValue) = field->Get(static_cast<Behaviour*>(script));
        return true;
    }

    static bool EngineServices_SetScriptField(void* script, const char* className, const char* fieldName, const void* value) {
        const FieldHandle* field = FindScriptField(className, fieldName);
        if (!script || !field || !value)
            return false;
        // Same alternative check the Properties panel relies on: a mismatched type is ignored.
        FieldValue current = field->Get(static_cast<Behaviour*>(script));
        const FieldValue& incoming = *static_cast<const FieldValue*>(value);
        if (current.index() != incoming.index())
            return false;
        field->Set(static_cast<Behaviour*>(script), incoming);
        return true;
    }

    static bool EngineServices_CallScriptMethod(void* script, const char* methodName) {
        return JsScripting::CallMethod(static_cast<Behaviour*>(script), methodName);
    }

    static int EngineServices_CharacterControllerMove(void* scenePtr, unsigned int entityHandle, float x, float y, float z) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        return scene ? PhysicsSystems::CharacterMove(*scene, EntityFromSceneHandle(scene, entityHandle), { x, y, z }) : 0;
    }

    static bool EngineServices_CharacterControllerSimpleMove(void* scenePtr, unsigned int entityHandle, float x, float y, float z) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        return scene && PhysicsSystems::CharacterSimpleMove(*scene, EntityFromSceneHandle(scene, entityHandle), { x, y, z });
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
        *outX = PhysicsUnits::ToWorld(v.x);
        *outY = PhysicsUnits::ToWorld(v.y);
        return true;
    }

    static void EngineServices_SetVelocity2D(void* scenePtr, unsigned int entityHandle, float x, float y) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody2DComponent>(handle))
            return;
        void* runtimeBody = registry.get<Rigidbody2DComponent>(handle).RuntimeBody;
        if (runtimeBody)
            static_cast<b2Body*>(runtimeBody)->SetLinearVelocity(b2Vec2(PhysicsUnits::ToPhysics(x), PhysicsUnits::ToPhysics(y)));
    }

    static void EngineServices_AddForce2D(void* scenePtr, unsigned int entityHandle, float x, float y) {
        auto& registry = static_cast<Scene*>(scenePtr)->Registry();
        entt::entity handle = static_cast<entt::entity>(entityHandle);
        if (!registry.valid(handle) || !registry.all_of<Rigidbody2DComponent>(handle))
            return;
        void* runtimeBody = registry.get<Rigidbody2DComponent>(handle).RuntimeBody;
        if (runtimeBody)
            static_cast<b2Body*>(runtimeBody)->ApplyForceToCenter(b2Vec2(PhysicsUnits::ToPhysics(x), PhysicsUnits::ToPhysics(y)), true); // true = wake the body
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
        *outX = PhysicsUnits::ToWorld(v.x());
        *outY = PhysicsUnits::ToWorld(v.y());
        *outZ = PhysicsUnits::ToWorld(v.z());
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
            body->setLinearVelocity(btVector3(PhysicsUnits::ToPhysics(x), PhysicsUnits::ToPhysics(y), PhysicsUnits::ToPhysics(z)));
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
            body->applyCentralForce(btVector3(PhysicsUnits::ToPhysics(x), PhysicsUnits::ToPhysics(y), PhysicsUnits::ToPhysics(z)));
        }
    }

    static int EngineServices_GetPointerScreen() {
        return static_cast<int>(InputManager::GetPointerScreen());
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

    static void EngineServices_AudioSourcePlay(void* scenePtr, unsigned int entityHandle) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        Entity entity = EntityFromSceneHandle(scene, entityHandle);
        if (!entity)
            return;
        PlayAudioSourceOnEntity(scene, entity);
    }

    static void EngineServices_AudioSourceStop(void* scenePtr, unsigned int entityHandle) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        Entity entity = EntityFromSceneHandle(scene, entityHandle);
        if (!entity || !entity.HasComponent<AudioSourceComponent>())
            return;
        auto& src = entity.GetComponent<AudioSourceComponent>();
        if (src.RuntimeHandle != InvalidAudioHandle) {
            AudioEngine::Stop(src.RuntimeHandle);
            src.RuntimeHandle = InvalidAudioHandle;
        }
        src.Paused = false;
    }

    static void EngineServices_AudioSourceSetPaused(void* scenePtr, unsigned int entityHandle, bool paused) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        Entity entity = EntityFromSceneHandle(scene, entityHandle);
        if (!entity || !entity.HasComponent<AudioSourceComponent>())
            return;
        auto& src = entity.GetComponent<AudioSourceComponent>();
        if (src.RuntimeHandle == InvalidAudioHandle)
            return;
        src.Paused = paused;
        AudioEngine::SetPaused(src.RuntimeHandle, paused);
    }

    static void EngineServices_AudioSourceSetVolume(void* scenePtr, unsigned int entityHandle, float volume) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        Entity entity = EntityFromSceneHandle(scene, entityHandle);
        if (!entity || !entity.HasComponent<AudioSourceComponent>())
            return;
        auto& src = entity.GetComponent<AudioSourceComponent>();
        src.Volume = volume;
        if (src.RuntimeHandle != InvalidAudioHandle)
            AudioEngine::SetVolume(src.RuntimeHandle, volume);
    }

    static bool EngineServices_AudioSourceIsPlaying(void* scenePtr, unsigned int entityHandle) {
        Scene* scene = static_cast<Scene*>(scenePtr);
        Entity entity = EntityFromSceneHandle(scene, entityHandle);
        if (!entity || !entity.HasComponent<AudioSourceComponent>())
            return false;
        auto& src = entity.GetComponent<AudioSourceComponent>();
        if (src.RuntimeHandle == InvalidAudioHandle)
            return false;
        return AudioEngine::IsPlaying(src.RuntimeHandle);
    }

    static bool EngineServices_GetParentEntity(void* scenePtr, unsigned int entityHandle, unsigned int* outHandle) {
        if (!scenePtr || !outHandle)
            return false;
        Scene* scene = static_cast<Scene*>(scenePtr);
        Entity parent = scene->GetParent(EntityFromSceneHandle(scene, entityHandle));
        if (!parent)
            return false;
        *outHandle = static_cast<unsigned int>(parent.Handle());
        return true;
    }

    static int EngineServices_GetChildCount(void* scenePtr, unsigned int entityHandle) {
        if (!scenePtr)
            return 0;
        Scene* scene = static_cast<Scene*>(scenePtr);
        return static_cast<int>(scene->GetChildren(EntityFromSceneHandle(scene, entityHandle)).size());
    }

    static bool EngineServices_GetChildAt(void* scenePtr, unsigned int entityHandle, int index, unsigned int* outHandle) {
        if (!scenePtr || !outHandle || index < 0)
            return false;
        Scene* scene = static_cast<Scene*>(scenePtr);
        std::vector<Entity> children = scene->GetChildren(EntityFromSceneHandle(scene, entityHandle));
        if (static_cast<size_t>(index) >= children.size() || !children[static_cast<size_t>(index)])
            return false;
        *outHandle = static_cast<unsigned int>(children[static_cast<size_t>(index)].Handle());
        return true;
    }

    static void EngineServices_ClearEntityChildren(void* scenePtr, unsigned int entityHandle) {
        if (!scenePtr)
            return;
        Scene* scene = static_cast<Scene*>(scenePtr);
        scene->ClearChildren(EntityFromSceneHandle(scene, entityHandle));
    }

    static const EngineServices s_EngineServices = {
        &EngineServices_GetKey,
        &EngineServices_GetKeyDown,
        &EngineServices_GetKeyUp,
        &EngineServices_GetAxis,
        &EngineServices_GetPointerDown,
        &EngineServices_GetPointerPosition,
        &EngineServices_GetDeltaTime,
        &EngineServices_GetElapsedTime,
        &EngineServices_GetFrameCount,
        &EngineServices_PlaySound,
        &EngineServices_StopAllSounds,
        &EngineServices_FindEntityInScreen,
        &EngineServices_LogInfo,
        &EngineServices_LogWarn,
        &EngineServices_LogError,
        &EngineServices_RequestLoadScene,
        &EngineServices_RequestUnloadScene,
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
        &EngineServices_AudioSourcePlay,
        &EngineServices_AudioSourceStop,
        &EngineServices_AudioSourceSetPaused,
        &EngineServices_AudioSourceSetVolume,
        &EngineServices_AudioSourceIsPlaying,
        &EngineServices_GetParentEntity,
        &EngineServices_GetChildCount,
        &EngineServices_GetChildAt,
        &EngineServices_ClearEntityChildren,
        &EngineServices_DestroyEntity,
        &EngineServices_FindScript,
        &EngineServices_GetScriptField,
        &EngineServices_SetScriptField,
        &EngineServices_CallScriptMethod,
        &EngineServices_CharacterControllerMove,
        &EngineServices_CharacterControllerSimpleMove,
    };

    namespace {
        // ScriptContext uses thread-local-like static state. Scope it so a desktop
        // exception cannot leave the next callback bound to the wrong entity.
        class ScriptContextScope {
        public:
            ScriptContextScope(Scene* scene, entt::entity entity) {
                ScriptContext::Bind(&s_EngineServices, scene, static_cast<unsigned int>(entity));
            }
            ~ScriptContextScope() { ScriptContext::Clear(); }
        };

        // A callback may append script slots to its own entity (Scene::AddRuntimeScript), which
        // can reallocate BehaviourComponent::Scripts -- so after a callback returns, the slot is
        // re-found by its Instance pointer instead of trusting the reference passed in.
        ScriptInstance* FindSlotByInstance(Scene* scene, entt::entity entity, Behaviour* instance) {
            if (!instance || !scene->Registry().valid(entity))
                return nullptr;
            auto* bc = scene->Registry().try_get<BehaviourComponent>(entity);
            if (!bc)
                return nullptr;
            for (auto& slot : bc->Scripts)
                if (slot.Instance == instance)
                    return &slot;
            return nullptr;
        }

        template<typename TCallback>
        bool InvokeScriptCallback(Scene* scene, entt::entity entity, ScriptInstance& script,
                                  const char* callbackName, TCallback&& callback) {
            ScriptContextScope context(scene, entity);
            // Profiler: C++ and TypeScript script time reported separately (compiled out on 3DS).
            DUALITY_PROFILE_SCOPE(Profiler::IsRecording() && JsScripting::IsTypeScriptInstance(script.Instance) ? "Scripts.TypeScript" : "Scripts.C++");
#if defined(DE_PLATFORM_DESKTOP) && defined(__cpp_exceptions)
            Behaviour* instance = script.Instance;
            const std::string className = script.ClassName;
            try {
                callback();
                return true;
            } catch (const std::exception& exception) {
                Log::Error("Script '" + className + "' on entity " +
                    std::to_string(static_cast<uint32_t>(entity)) + " threw in " + callbackName +
                    ": " + exception.what() + ". Script disabled for this Play session.");
            } catch (...) {
                Log::Error("Script '" + className + "' on entity " +
                    std::to_string(static_cast<uint32_t>(entity)) + " threw an unknown exception in " +
                    callbackName + ". Script disabled for this Play session.");
            }
            if (ScriptInstance* slot = FindSlotByInstance(scene, entity, instance)) {
                slot->Enabled = false;
                slot->WasEnabledLastFrame = false;
            }
            return false;
#else
            // devkitARM builds commonly disable C++ exceptions to keep code and
            // unwind tables small. Validation and safe component APIs remain
            // active there; native memory faults cannot be recovered from.
            callback();
            return true;
#endif
        }
    }

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

    std::vector<Entity> Scene::GetHierarchyTraversalOrder() const {
        std::vector<Entity> ordered;
        std::set<entt::entity> visited;

        // The visited set is normally redundant (Scene::SetParent prevents cycles), but makes
        // this rendering/editor utility safe even if a malformed scene file or future tool
        // creates a bad hierarchy relationship.
        auto visit = [&](auto&& self, Entity entity) -> void {
            if (!entity || !m_Registry.valid(entity.Handle()) || !visited.insert(entity.Handle()).second)
                return;
            ordered.push_back(entity);
            const HierarchyComponent& hierarchy = m_Registry.get<HierarchyComponent>(entity.Handle());
            for (Entity child : hierarchy.Children)
                self(self, child);
        };

        for (Entity root : m_RootEntities)
            visit(visit, root);

        // A well-formed Scene is completely reachable from m_RootEntities. Include any orphan
        // entity as a deterministic trailing fallback instead of making it disappear merely
        // because a tool loaded an incomplete hierarchy.
        for (auto handle : m_Registry.view<HierarchyComponent>())
            visit(visit, Entity(handle, const_cast<Scene*>(this)));

        return ordered;
    }

    Entity Scene::GetParent(Entity entity) const {
        if (!entity || entity.GetScene() != this || !m_Registry.valid(entity.Handle()) ||
            !m_Registry.all_of<HierarchyComponent>(entity.Handle()))
            return Entity{};
        return m_Registry.get<HierarchyComponent>(entity.Handle()).Parent;
    }

    std::vector<Entity> Scene::GetChildren(Entity entity) const {
        if (!entity || entity.GetScene() != this || !m_Registry.valid(entity.Handle()) ||
            !m_Registry.all_of<HierarchyComponent>(entity.Handle()))
            return {};
        return m_Registry.get<HierarchyComponent>(entity.Handle()).Children;
    }

    void Scene::ClearChildren(Entity parent) {
        if (!parent || parent.GetScene() != this || !m_Registry.valid(parent.Handle()) ||
            !m_Registry.all_of<HierarchyComponent>(parent.Handle()))
            return;
        const std::vector<Entity> children = m_Registry.get<HierarchyComponent>(parent.Handle()).Children;
        for (Entity child : children) {
            if (child && child.GetScene() == this && m_Registry.valid(child.Handle()))
                DestroyEntity(child);
        }
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

    void Scene::SetSiblingIndex(Entity child, Entity newParent, std::size_t siblingIndex, bool preserveWorldPosition) {
        if (!child || child == newParent)
            return;
        if (newParent && IsDescendantOf(newParent, child))
            return; // would create a cycle

        auto& childHierarchy = child.GetComponent<HierarchyComponent>();
        Entity oldParent = childHierarchy.Parent;
        TransformComponent worldBefore = GetWorldTransform(child);

        std::vector<Entity>& oldSiblings = SiblingListFor(oldParent);
        oldSiblings.erase(std::remove(oldSiblings.begin(), oldSiblings.end(), child), oldSiblings.end());

        std::vector<Entity>& newSiblings = SiblingListFor(newParent);
        siblingIndex = std::min(siblingIndex, newSiblings.size());
        newSiblings.insert(newSiblings.begin() + static_cast<std::ptrdiff_t>(siblingIndex), child);
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
        Layer targetLayer = ScreenToLayer(screen);
        Entity root = GetScreenRoot(screen);
        if (root)
            return FindByNameInSubtree(root, name);

        // No LayerComponent adopted for this screen yet -- fall back to a
        // scene-wide by-name search so the API isn't a no-op out of the box.
        for (auto handle : m_Registry.view<NameComponent>()) {
            if (m_Registry.get<NameComponent>(handle).Name == name)
                return Entity(handle, this);
        }
        return Entity{};
    }

    bool Scene::TryResolveEntityScreen(Entity entity, Screen& outScreen) {
        return LayerToScreen(ResolveEntityLayer(entity), outScreen);
    }

    Layer Scene::ResolveEntityLayer(Entity entity) {
        Entity current = entity;
        while (current) {
            // Default is deliberately not an override: it means "inherit from
            // my parent when one exists". Thus a single LayerComponent on the
            // Top/Bottom root governs its full subtree; add TOP/BOTTOM on a
            // descendant only when it intentionally needs to cross screens.
            if (current.HasComponent<LayerComponent>()) {
                Layer layer = current.GetComponent<LayerComponent>().Value;
                if (layer != Layer::Default)
                    return layer;
            }
            if (current.HasComponent<CameraComponent>())
                return ScreenToLayer(current.GetComponent<CameraComponent>().Screen);
            current = current.GetComponent<HierarchyComponent>().Parent;
        }
        return Layer::Default;
    }

    Entity Scene::GetPrimaryCamera(Screen screen) {
        auto view = m_Registry.view<CameraComponent>();
        for (auto handle : view) {
            const auto& camera = view.get<CameraComponent>(handle);
            if (camera.Enabled && camera.Screen == screen && camera.Primary)
                return Entity(handle, this);
        }
        return Entity{};
    }

    Entity Scene::GetScreenRoot(Screen screen) {
        const Layer targetLayer = ScreenToLayer(screen);
        for (Entity root : m_RootEntities) {
            if (root.HasComponent<LayerComponent>() &&
                root.GetComponent<LayerComponent>().Value == targetLayer)
                return root;
        }
        return Entity{};
    }

    Entity Scene::EnsureScreenRoot(Screen screen) {
        if (Entity root = GetScreenRoot(screen))
            return root;

        Entity root = CreateEntity(screen == Screen::Top ? "Top" : "Bottom");
        root.AddComponent<LayerComponent>().Value = ScreenToLayer(screen);
        return root;
    }

    namespace {
        static void ScreenExtents(Screen screen, float& outWidth, float& outHeight) {
            if (screen == Screen::Top) {
                outWidth = static_cast<float>(TopScreenWidth);
                outHeight = static_cast<float>(TopScreenHeight);
            } else {
                outWidth = static_cast<float>(BottomScreenWidth);
                outHeight = static_cast<float>(BottomScreenHeight);
            }
        }
    }

    bool Scene::ScreenPointToRay3D(Entity cameraEntity, const glm::vec2& screenPoint, glm::vec3& outOrigin, glm::vec3& outDirection) {
        if (!cameraEntity || !cameraEntity.HasComponent<CameraComponent>())
            return false;
        auto& cameraComponent = cameraEntity.GetComponent<CameraComponent>();

        TransformComponent camTransform = GetWorldTransform(cameraEntity);
        float screenWidth, screenHeight;
        ScreenExtents(cameraComponent.Screen, screenWidth, screenHeight);
        float aspect = screenWidth / screenHeight;

        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, glm::radians(camTransform.Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        rot = glm::rotate(rot, glm::radians(camTransform.Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(camTransform.Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 forward = glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 right = glm::vec3(rot * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        glm::vec3 up = glm::vec3(rot * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));

        float ndcX = (2.0f * (screenPoint.x / screenWidth)) - 1.0f;
        float ndcY = 1.0f - (2.0f * (screenPoint.y / screenHeight));

        if (cameraComponent.Projection == ProjectionType::Orthographic) {
            // Keep this in lockstep with SceneRenderer/OpenGLRenderer3D's ortho
            // projection: bottom is +halfHeight and top is -halfHeight, so NDC +Y
            // is camera up.  Unlike 2D sprite coordinates, 3D orthographic size is
            // expressed directly in world units and intentionally does not use PPU.
            float halfHeight = screenHeight * 0.5f / std::max(cameraComponent.Zoom, 0.0001f);
            float halfWidth = halfHeight * aspect;
            outOrigin = camTransform.Translation + right * (ndcX * halfWidth) + up * (ndcY * halfHeight);
            outDirection = glm::normalize(forward);
            return true;
        }

        float tanHalfFov = std::tan(glm::radians(cameraComponent.FovDegrees) * 0.5f);

        outOrigin = camTransform.Translation;
        outDirection = glm::normalize(forward + right * (ndcX * tanHalfFov * aspect) + up * (ndcY * tanHalfFov));
        return true;
    }

    bool Scene::ScreenPointToRay3D(Screen screen, const glm::vec2& screenPoint, glm::vec3& outOrigin, glm::vec3& outDirection) {
        Entity camera = GetPrimaryCamera(screen);
        if (!camera)
            return false;
        return ScreenPointToRay3D(camera, screenPoint, outOrigin, outDirection);
    }

    bool Scene::ScreenPointToWorld2D(Entity cameraEntity, const glm::vec2& screenPoint, glm::vec2& outWorld) {
        if (!cameraEntity || !cameraEntity.HasComponent<CameraComponent>())
            return false;
        auto& cameraComponent = cameraEntity.GetComponent<CameraComponent>();
        if (cameraComponent.Projection != ProjectionType::Orthographic)
            return false;

        TransformComponent camTransform = GetWorldTransform(cameraEntity);
        float screenWidth, screenHeight;
        ScreenExtents(cameraComponent.Screen, screenWidth, screenHeight);
        const float worldToPixels = cameraComponent.Zoom * PhysicsUnits::PPU();
        outWorld.x = (screenPoint.x - screenWidth * 0.5f) / worldToPixels + camTransform.Translation.x;
        outWorld.y = (screenPoint.y - screenHeight * 0.5f) / worldToPixels + camTransform.Translation.y;
        return true;
    }

    void Scene::OnRuntimeStart() {
        m_PhysicsAccumulator = 0.0f;
        s_RuntimeDeltaTime = 0.0f;
        s_RuntimeElapsedTime = 0.0f;
        s_RuntimeFrameCount = 0;

        for (auto handle : m_Registry.view<AnimationComponent>()) {
            auto& animation = m_Registry.get<AnimationComponent>(handle);
            animation.Time = 0.0f;
            animation.Playing = animation.PlayOnAwake;
        }

        CreatePhysicsWorld2D(); // Physics2DSystem.cpp
        CreatePhysicsWorld3D(); // Physics3DSystem.cpp

        CreatePhysicsBodies(nullptr);

        m_Running = true;
        m_PendingDestroy.clear();

        // Copy the handle list first: an OnCreate may Instantiate a prefab (whose scripts are
        // then created by CreateRuntimeScripts itself) while this loop is still running.
        std::vector<entt::entity> scriptedEntities;
        for (auto handle : m_Registry.view<BehaviourComponent>())
            scriptedEntities.push_back(handle);
        for (auto handle : scriptedEntities)
            CreateScriptsOn(handle);

        for (auto handle : m_Registry.view<AudioSourceComponent>()) {
            auto& src = m_Registry.get<AudioSourceComponent>(handle);
            if (src.Enabled && src.PlayOnAwake)
                PlayAudioSourceOnEntity(this, Entity(handle, this));
        }
    }

    // Creates the Box2D/Bullet bodies for every eligible entity (OnRuntimeStart: `only` ==
    // nullptr), or only for the listed entities (a prefab spawned during Play -- see
    // CreateRuntimeScripts). Entities that already have a body are skipped either way.
    void Scene::CreatePhysicsBodies(const std::vector<entt::entity>* only) {
        if (!m_PhysicsWorld || !m_PhysicsWorld3D)
            return;
        CreatePhysicsBodies2D(only); // Physics2DSystem.cpp
        CreatePhysicsBodies3D(only); // Physics3DSystem.cpp
    }

    void Scene::CreateScriptsOn(entt::entity handle) {
        // Index-based on purpose: an OnCreate may AddRuntimeScript onto its own entity, which
        // appends to (and may reallocate) this very vector.
        for (size_t i = 0;; ++i) {
            if (!m_Registry.valid(handle))
                return;
            auto* bc = m_Registry.try_get<BehaviourComponent>(handle);
            if (!bc || i >= bc->Scripts.size())
                return;
            ScriptInstance& script = bc->Scripts[i];
            if (script.Instance || script.ClassName.empty())
                continue;

            ScriptRegistry::ResolvePendingOverrides(script);
            if (ScriptRegistry::TryCreate(script.ClassName, &script.Instance, &script.Destroy)) {
                script.Instance->m_Entity = Entity(handle, this);
                script.Instance->m_Enabled = &script.Enabled;
                script.Instance->SetEngineServices(&s_EngineServices);

                // Apply Edit-mode Inspector overrides (see ScriptInstance's own comment)
                // onto the fresh instance's real fields before OnCreate() sees them --
                // matches Unity's own field-initialization-before-Awake ordering.
                const std::vector<FieldHandle>& fields = ScriptRegistry::GetFields(script.ClassName);
                for (auto& [name, value] : script.PropertyOverrides) {
                    for (auto& field : fields) {
                        if (field.Name == name) {
                            field.Set(script.Instance, value);
                            break;
                        }
                    }
                }

                InvokeScriptCallback(this, handle, script, "OnCreate", [&]() {
                    script.Instance->OnCreate();
                });
            } else {
                Log::Error("Behaviour: unknown script class '" + script.ClassName + "'");
            }
        }
    }

    void Scene::CreateRuntimeScripts(Entity root) {
        if (!m_Running || !root || !m_Registry.valid(root.Handle()))
            return;
        // Parent first, then children (depth-first) -- Unity Awakes a spawned hierarchy
        // top-down too.
        std::vector<entt::entity> subtree;
        std::vector<Entity> stack{ root };
        while (!stack.empty()) {
            Entity current = stack.back();
            stack.pop_back();
            if (!m_Registry.valid(current.Handle()))
                continue;
            subtree.push_back(current.Handle());
            if (auto* hierarchy = m_Registry.try_get<HierarchyComponent>(current.Handle()))
                for (auto it = hierarchy->Children.rbegin(); it != hierarchy->Children.rend(); ++it)
                    stack.push_back(*it);
        }

        // Physics bodies first, so an Awake can already use its Rigidbody.
        CreatePhysicsBodies(&subtree);
        for (entt::entity handle : subtree)
            if (m_Registry.valid(handle))
                CreateScriptsOn(handle);
        for (entt::entity handle : subtree) {
            auto* src = m_Registry.valid(handle) ? m_Registry.try_get<AudioSourceComponent>(handle) : nullptr;
            if (src && src->Enabled && src->PlayOnAwake)
                PlayAudioSourceOnEntity(this, Entity(handle, this));
        }
    }

    Behaviour* Scene::AddRuntimeScript(Entity entity, const std::string& className) {
        if (!entity || !m_Registry.valid(entity.Handle()) || className.empty())
            return nullptr;
        auto& bc = m_Registry.all_of<BehaviourComponent>(entity.Handle())
            ? m_Registry.get<BehaviourComponent>(entity.Handle())
            : m_Registry.emplace<BehaviourComponent>(entity.Handle());
        bc.Scripts.push_back(ScriptInstance{ className });
        // push_back may have moved every slot: live instances point at their slot's Enabled.
        for (auto& slot : bc.Scripts)
            if (slot.Instance)
                slot.Instance->m_Enabled = &slot.Enabled;
        if (!m_Running)
            return nullptr;
        const size_t index = bc.Scripts.size() - 1;
        CreateScriptsOn(entity.Handle());
        auto* after = m_Registry.try_get<BehaviourComponent>(entity.Handle());
        return after && index < after->Scripts.size() ? after->Scripts[index].Instance : nullptr;
    }

    void Scene::DestroyEntityDeferred(Entity entity) {
        if (!entity || !m_Registry.valid(entity.Handle()))
            return;
        if (!m_Running) {
            DestroyEntity(entity);
            return;
        }
        if (std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), entity.Handle()) == m_PendingDestroy.end())
            m_PendingDestroy.push_back(entity.Handle());
    }

    TransformComponent Scene::WorldToLocal(Entity entity, const TransformComponent& world) {
        Entity parent = entity ? GetParent(entity) : Entity{};
        if (!parent)
            return world;
        return DecomposeWorld(world, GetWorldTransform(parent));
    }

    // Script teardown + physics-body release for ONE entity that is about to be removed
    // mid-Play (children are handled by the caller's subtree walk).
    void Scene::ReleaseRuntimeState(entt::entity handle) {
        if (auto* bc = m_Registry.try_get<BehaviourComponent>(handle)) {
            for (size_t i = 0;; ++i) {
                bc = m_Registry.try_get<BehaviourComponent>(handle);
                if (!bc || i >= bc->Scripts.size())
                    break;
                ScriptInstance& script = bc->Scripts[i];
                if (!script.Instance)
                    continue;
                Behaviour* instance = script.Instance;
                if (script.WasEnabledLastFrame)
                    InvokeScriptCallback(this, handle, script, "OnDisable", [&]() { instance->OnDisable(); });
                if (ScriptInstance* slot = FindSlotByInstance(this, handle, instance)) {
                    InvokeScriptCallback(this, handle, *slot, "OnDestroy", [&]() { instance->OnDestroy(); });
                    if (ScriptInstance* again = FindSlotByInstance(this, handle, instance)) {
                        again->WasEnabledLastFrame = false;
                        if (again->Destroy)
                            again->Destroy(instance);
                        again->Instance = nullptr;
                    }
                }
            }
        }

        // Physics2DSystem.cpp / Physics3DSystem.cpp (joints first, then the body).
        ReleasePhysics2D(handle);
        ReleasePhysics3D(handle);
    }

    void Scene::FlushPendingDestroys() {
        // OnDestroy handlers may Destroy more entities -- keep draining until stable.
        while (!m_PendingDestroy.empty()) {
            std::vector<entt::entity> batch;
            batch.swap(m_PendingDestroy);
            for (entt::entity root : batch) {
                if (!m_Registry.valid(root))
                    continue;
                // Children first (post-order), so a child's OnDestroy still sees its parent.
                std::vector<entt::entity> order;
                std::vector<entt::entity> stack{ root };
                while (!stack.empty()) {
                    entt::entity current = stack.back();
                    stack.pop_back();
                    order.push_back(current);
                    if (auto* hierarchy = m_Registry.try_get<HierarchyComponent>(current))
                        for (Entity child : hierarchy->Children)
                            if (m_Registry.valid(child.Handle()))
                                stack.push_back(child.Handle());
                }
                for (auto it = order.rbegin(); it != order.rend(); ++it)
                    if (m_Registry.valid(*it))
                        ReleaseRuntimeState(*it);
                if (m_Registry.valid(root))
                    DestroyEntity(Entity(root, this));
            }
        }
    }

    void Scene::OnRuntimeUpdate(float deltaTime) {
        // Updated before any physics/collision/script callback so Time reports
        // this exact frame consistently from every callback type.
        s_RuntimeDeltaTime = std::max(0.0f, deltaTime);
        s_RuntimeElapsedTime += s_RuntimeDeltaTime;
        ++s_RuntimeFrameCount;
        // Filled by Box2DContactListener (repointed here for the duration of this one
        // Step call) and/or the Bullet manifold-diff below, then dispatched to
        // Behaviour::OnCollisionEnter/Exit/OnTriggerEnter/Exit right after both physics
        // steps -- see PhysicsContactEvent's own comment.
        std::vector<PhysicsContactEvent> contactEvents;

        // Physics steps at a FIXED 1/60s timestep, accumulated from the real (variable) frame
        // deltaTime, instead of a single Step(deltaTime, ...) call using whatever deltaTime this
        // function happened to be called with. Box2D/Bullet's own semi-implicit Euler
        // integration accumulates real, measurable error at larger timesteps -- confirmed as the
        // actual cause of a real reported bug (a platformer's jump apex was noticeably LOWER on
        // real 3DS hardware, which runs at a real framerate far below the Editor's desktop
        // framerate, than in the Editor's own Play-in-Editor preview, for the exact same
        // JumpSpeed impulse). A fixed timestep, substepped as many times as needed to consume
        // however much real time actually elapsed, keeps physics behavior IDENTICAL regardless
        // of the host's actual framerate, matching Unity's own FixedUpdate/Godot's physics
        // tick/every other serious game engine's own established fix for this exact class of
        // bug. Capped to MaxStepsPerFrame worth of accumulated time so a real hitch (e.g. asset
        // loading, a debugger breakpoint) can't force a "spiral of death" of ever-more substeps
        // each subsequent frame -- time beyond that cap is simply dropped, matching the standard
        // accepted tradeoff (physics runs in slow motion during a severe hitch, rather than
        // hanging entirely trying to catch up). Scripts still see exactly one OnUpdate(realDelta
        // Time) call per real frame at the end of this function, unaffected -- only the physics
        // sub-block above it runs 0-N times per call.
        constexpr float FixedDeltaTime = 1.0f / 60.0f;
        constexpr int MaxStepsPerFrame = 5;
        m_PhysicsAccumulator = std::min(m_PhysicsAccumulator + deltaTime, FixedDeltaTime * MaxStepsPerFrame);

        while (m_PhysicsAccumulator >= FixedDeltaTime) {
            m_PhysicsAccumulator -= FixedDeltaTime;
            DUALITY_PROFILE_SCOPE("Physics.FixedStep"); // FixedUpdate scripts + Box2D/Bullet step

            // Unity's FixedUpdate: once per substep, BEFORE it is simulated, so velocities/
            // forces a script sets here are integrated by this very step. Only scripts that
            // already received OnEnable (WasEnabledLastFrame) -- a script spawned this frame
            // gets Awake -> OnEnable -> Start first, exactly like Unity's ordering.
            {
                auto fixedView = m_Registry.view<BehaviourComponent>();
                for (auto handle : fixedView) {
                    auto& bc = fixedView.get<BehaviourComponent>(handle);
                    for (size_t i = 0; i < bc.Scripts.size(); ++i) {
                        ScriptInstance& script = bc.Scripts[i];
                        if (!script.Instance || !script.Enabled || !script.WasEnabledLastFrame)
                            continue;
                        InvokeScriptCallback(this, handle, script, "OnFixedUpdate", [&]() {
                            script.Instance->OnFixedUpdate(FixedDeltaTime);
                        });
                        if (!m_Registry.valid(handle) || !m_Registry.all_of<BehaviourComponent>(handle))
                            break;
                    }
                }
            }

            // Physics2DSystem.cpp / Physics3DSystem.cpp: push Transforms, step, sync back, contacts.
            StepPhysics2D(FixedDeltaTime, contactEvents);
            StepPhysics3D(FixedDeltaTime, contactEvents);
        }

        // Dispatch collision/trigger events to both sides of each pair (Unity's own
        // convention -- each side's script, if any, gets called with the OTHER entity).
        // m_Registry.valid() guards against an entity destroyed by an earlier event's own
        // handler this same frame.
        for (auto& event : contactEvents) {
            if (!m_Registry.valid(event.A) || !m_Registry.valid(event.B))
                continue;
            auto fire = [this](entt::entity self, Entity other, bool isTrigger, bool isBegin) {
                // Index-based: a handler may AddRuntimeScript onto its own entity.
                for (size_t i = 0;; ++i) {
                    if (!m_Registry.valid(self))
                        return;
                    auto* bc = m_Registry.try_get<BehaviourComponent>(self);
                    if (!bc || i >= bc->Scripts.size())
                        return;
                    ScriptInstance& script = bc->Scripts[i];
                    if (!script.Instance || !script.Enabled)
                        continue;
                    Behaviour* instance = script.Instance;
                    InvokeScriptCallback(this, self, script, isTrigger
                        ? (isBegin ? "OnTriggerEnter" : "OnTriggerExit")
                        : (isBegin ? "OnCollisionEnter" : "OnCollisionExit"), [&]() {
                            if (isTrigger) {
                                if (isBegin) instance->OnTriggerEnter(other); else instance->OnTriggerExit(other);
                            } else {
                                if (isBegin) instance->OnCollisionEnter(other); else instance->OnCollisionExit(other);
                            }
                        });
                }
            };
            fire(event.A, Entity(event.B, this), event.IsTrigger, event.IsBegin);
            fire(event.B, Entity(event.A, this), event.IsTrigger, event.IsBegin);
        }

        auto flipbookView = m_Registry.view<SpriteFlipbookComponent>();
        for (auto handle : flipbookView) {
            auto& flipbook = flipbookView.get<SpriteFlipbookComponent>(handle);
            if (!flipbook.Enabled || !flipbook.Playing)
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

        {
            DUALITY_PROFILE_SCOPE("Scene.RuntimeSystems"); // animation, sprite sheets, particles, UI layout
            UpdateSceneRuntimeSystems(*this, deltaTime);
        }

        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            if (behaviourView.get<BehaviourComponent>(handle).Scripts.empty())
                continue;

            bool entityActive = IsEffectivelyActive(Entity(handle, this));
            // Index-based, re-finding the slot after every callback: a script may
            // AddRuntimeScript onto its own entity, reallocating Scripts mid-loop.
            for (size_t i = 0;; ++i) {
                auto* bc = m_Registry.try_get<BehaviourComponent>(handle);
                if (!bc || i >= bc->Scripts.size())
                    break;
                Behaviour* instance = bc->Scripts[i].Instance;
                if (!instance)
                    continue;
                bool enabled = entityActive && bc->Scripts[i].Enabled;
                if (enabled != bc->Scripts[i].WasEnabledLastFrame) {
                    InvokeScriptCallback(this, handle, bc->Scripts[i], enabled ? "OnEnable" : "OnDisable", [&]() {
                        if (enabled)
                            instance->OnEnable();
                        else
                            instance->OnDisable();
                    });
                    ScriptInstance* slot = FindSlotByInstance(this, handle, instance);
                    if (!slot)
                        continue;
                    slot->WasEnabledLastFrame = entityActive && slot->Enabled;
                }
                ScriptInstance* slot = FindSlotByInstance(this, handle, instance);
                if (slot && entityActive && slot->Enabled) {
                    InvokeScriptCallback(this, handle, *slot, "OnUpdate", [&]() {
                        instance->OnUpdate(deltaTime);
                    });
                }
            }
        }

        // Unity's LateUpdate: a second pass once EVERY script has updated this frame.
        for (auto handle : behaviourView) {
            if (!IsEffectivelyActive(Entity(handle, this)))
                continue;
            for (size_t i = 0;; ++i) {
                auto* bc = m_Registry.try_get<BehaviourComponent>(handle);
                if (!bc || i >= bc->Scripts.size())
                    break;
                ScriptInstance& script = bc->Scripts[i];
                if (!script.Instance || !script.Enabled || !script.WasEnabledLastFrame)
                    continue;
                Behaviour* instance = script.Instance;
                InvokeScriptCallback(this, handle, script, "OnLateUpdate", [&]() {
                    instance->OnLateUpdate(deltaTime);
                });
            }
        }

        // Constraints, Billboard, Trail Renderer sampling -- after every LateUpdate, like Unity.
        UpdateLateRuntimeSystems(*this, deltaTime);

        // TypeScript coroutines / invoke() timers advance once per frame, after LateUpdate.
        JsScripting::OnSceneFrameEnd(this);

        // Unity destroys Object.Destroy'd objects at the end of the frame.
        FlushPendingDestroys();
    }

    void Scene::OnApplicationFocus(bool focused) {
        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            if (!IsEffectivelyActive(Entity(handle, this)))
                continue;
            auto& behaviours = behaviourView.get<BehaviourComponent>(handle);
            for (auto& script : behaviours.Scripts) {
                if (!script.Instance || !script.Enabled)
                    continue;
                InvokeScriptCallback(this, handle, script, focused ? "OnApplicationFocus(true)" : "OnApplicationFocus(false)", [&]() {
                    script.Instance->OnApplicationFocus(focused);
                });
            }
        }
    }

    void Scene::OnApplicationPause(bool paused) {
        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            if (!IsEffectivelyActive(Entity(handle, this)))
                continue;
            auto& behaviours = behaviourView.get<BehaviourComponent>(handle);
            for (auto& script : behaviours.Scripts) {
                if (!script.Instance || !script.Enabled)
                    continue;
                InvokeScriptCallback(this, handle, script, paused ? "OnApplicationPause(true)" : "OnApplicationPause(false)", [&]() {
                    script.Instance->OnApplicationPause(paused);
                });
            }
        }
    }

    void Scene::OnApplicationQuit() {
        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            auto& behaviours = behaviourView.get<BehaviourComponent>(handle);
            for (auto& script : behaviours.Scripts) {
                if (!script.Instance)
                    continue;
                InvokeScriptCallback(this, handle, script, "OnApplicationQuit", [&]() {
                    script.Instance->OnApplicationQuit();
                });
            }
        }
    }

    void Scene::OnRuntimeStop() {
        ClearSceneRuntimeSystems(*this);

        for (auto handle : m_Registry.view<AudioSourceComponent>()) {
            auto& src = m_Registry.get<AudioSourceComponent>(handle);
            if (src.RuntimeHandle != InvalidAudioHandle) {
                AudioEngine::Stop(src.RuntimeHandle);
                src.RuntimeHandle = InvalidAudioHandle;
            }
            src.Paused = false;
        }

        // Anything Destroy()ed during the last frame's callbacks is torn down like a normal
        // end-of-frame destroy first, then Play mode ends (no new runtime scripts after this).
        FlushPendingDestroys();
        m_Running = false;

        auto behaviourView = m_Registry.view<BehaviourComponent>();
        for (auto handle : behaviourView) {
            for (size_t i = 0;; ++i) {
                auto* bc = m_Registry.try_get<BehaviourComponent>(handle);
                if (!bc || i >= bc->Scripts.size())
                    break;
                Behaviour* instance = bc->Scripts[i].Instance;
                if (!instance)
                    continue;
                if (bc->Scripts[i].WasEnabledLastFrame)
                    InvokeScriptCallback(this, handle, bc->Scripts[i], "OnDisable", [&]() { instance->OnDisable(); });
                if (ScriptInstance* slot = FindSlotByInstance(this, handle, instance))
                    InvokeScriptCallback(this, handle, *slot, "OnDestroy", [&]() { instance->OnDestroy(); });
                if (ScriptInstance* slot = FindSlotByInstance(this, handle, instance)) {
                    if (slot->Destroy)
                        slot->Destroy(instance);
                    else
                        Log::Error("Script '" + slot->ClassName + "' has no destroy function.");
                    slot->Instance = nullptr;
                    slot->WasEnabledLastFrame = false;
                }
            }
        }
        m_PendingDestroy.clear();
        JsScripting::OnSceneStopped(this);

        // Physics2DSystem.cpp / Physics3DSystem.cpp.
        DestroyPhysicsWorld2D();
        DestroyPhysicsWorld3D();
    }

    void Scene::InvokeScriptCallbacks(entt::entity entity, const char* callbackName, void (*invoke)(Behaviour*, void*), void* user) {
        if (!invoke || !m_Registry.valid(entity) || !IsEffectivelyActive(Entity(entity, this)))
            return;
        // May run nested inside another script's call (CharacterController.Move fires
        // OnControllerColliderHit synchronously, like Unity): restore that caller's binding.
        void* const outerScene = ScriptContext::Scene();
        const unsigned int outerEntity = ScriptContext::EntityHandle();
        // Index-based like every other dispatch loop: a callback may add scripts to its entity.
        for (size_t i = 0;; ++i) {
            if (!m_Registry.valid(entity))
                break;
            auto* bc = m_Registry.try_get<BehaviourComponent>(entity);
            if (!bc || i >= bc->Scripts.size())
                break;
            ScriptInstance& script = bc->Scripts[i];
            if (!script.Instance || !script.Enabled)
                continue;
            Behaviour* instance = script.Instance;
            InvokeScriptCallback(this, entity, script, callbackName, [&]() { invoke(instance, user); });
        }
        if (outerScene)
            ScriptContext::Bind(&s_EngineServices, outerScene, outerEntity);
    }

    void Scene::Clear() {
        m_Registry.clear();
        m_RootEntities.clear();
    }

    namespace {

        // Behaviour deliberately does not inherit pointer interfaces. A script opts into each
        // event explicitly (`class MyScript : public Behaviour, public IPointerClickHandler`),
        // matching Unity's EventSystem contract and avoiding five unused vtable interfaces on
        // every behaviour. RTTI is available on both desktop and devkitARM builds.
        template<typename THandler>
        void DispatchPointerEvent(Scene& scene, entt::entity target, PointerEventData& eventData,
                                  void (THandler::*method)(PointerEventData&)) {
            if (!scene.Registry().valid(target) || !scene.Registry().all_of<BehaviourComponent>(target))
                return;
            if (!scene.IsEffectivelyActive(Entity(target, &scene)))
                return;
            auto& bc = scene.Registry().get<BehaviourComponent>(target);
            for (auto& script : bc.Scripts) {
                if (!script.Instance || !script.Enabled)
                    continue;
                auto* handler = dynamic_cast<THandler*>(script.Instance);
                if (!handler)
                    continue;
                InvokeScriptCallback(&scene, target, script, "pointer event", [&]() {
                    (handler->*method)(eventData);
                });
            }
        }

        using RaycastFromScreenFn = Entity (*)(Scene&, Entity, const glm::vec2&, float, glm::vec3&, float&);

        Entity RaycastHit2DFromCamera(Scene& scene, Entity cameraEntity, const glm::vec2& screenPoint, float /*maxDistance*/, glm::vec3& outWorldPoint, float& outDistance) {
            glm::vec2 world2D;
            if (!scene.ScreenPointToWorld2D(cameraEntity, screenPoint, world2D))
                return Entity{};
            RaycastHit2D hit = scene.RaycastPoint2D(world2D);
            outWorldPoint = { world2D.x, world2D.y, 0.0f };
            outDistance = hit ? hit.Distance : 0.0f;
            return hit.HitEntity;
        }

        Entity RaycastHit3DFromCamera(Scene& scene, Entity cameraEntity, const glm::vec2& screenPoint, float maxDistance, glm::vec3& outWorldPoint, float& outDistance) {
            glm::vec3 origin, direction;
            if (!scene.ScreenPointToRay3D(cameraEntity, screenPoint, origin, direction))
                return Entity{};
            RaycastHit3D hit = scene.Raycast3D(origin, direction, maxDistance);
            if (hit) {
                outWorldPoint = hit.Point;
                outDistance = hit.Distance;
                return hit.HitEntity;
            }
            outWorldPoint = origin + direction * maxDistance;
            outDistance = maxDistance;
            return Entity{};
        }

        struct RaycasterPointerState {
            entt::entity HoveredEntity = entt::null;
            entt::entity PressedEntity = entt::null;
            Screen PressedScreen = Screen::Top;
            glm::vec2 PressedPosition{ 0.0f };
        };

        void ProcessRaycaster(Scene& scene, Entity cameraEntity, Screen cameraScreen, bool enabled, RaycasterPointerState& state,
                              RaycastFromScreenFn raycastFn, float maxDistance,
                              const glm::vec2& pointer, Screen pointerScreen, bool pointerDown,
                              bool pointerDownEdge, bool pointerUpEdge) {
            if (!enabled)
                return;

            bool pointerOnThisScreen = pointerDown && pointerScreen == cameraScreen;
            Entity hitEntity;
            PointerEventData eventData;
            eventData.TargetScreen = cameraScreen;
            eventData.Position = pointer;

            if (pointerOnThisScreen) {
                hitEntity = raycastFn(scene, cameraEntity, pointer, maxDistance, eventData.WorldPoint, eventData.Distance);
                eventData.PointerCurrentRaycastTarget = hitEntity;
            } else {
                hitEntity = Entity{};
                eventData.PointerCurrentRaycastTarget = Entity{};
            }

            entt::entity hitHandle = hitEntity ? hitEntity.Handle() : entt::null;

            if (pointerOnThisScreen && hitHandle != state.HoveredEntity) {
                if (state.HoveredEntity != entt::null) {
                    eventData.PointerCurrentRaycastTarget = Entity(state.HoveredEntity, &scene);
                    DispatchPointerEvent(scene, state.HoveredEntity, eventData, &IPointerExitHandler::OnPointerExit);
                }
                if (hitHandle != entt::null) {
                    eventData.PointerCurrentRaycastTarget = hitEntity;
                    DispatchPointerEvent(scene, hitHandle, eventData, &IPointerEnterHandler::OnPointerEnter);
                }
                state.HoveredEntity = hitHandle;
            }

            if (!pointerOnThisScreen && state.HoveredEntity != entt::null) {
                eventData.PointerCurrentRaycastTarget = Entity(state.HoveredEntity, &scene);
                DispatchPointerEvent(scene, state.HoveredEntity, eventData, &IPointerExitHandler::OnPointerExit);
                state.HoveredEntity = entt::null;
            }

            if (pointerDownEdge && pointerScreen == cameraScreen) {
                hitEntity = raycastFn(scene, cameraEntity, pointer, maxDistance, eventData.WorldPoint, eventData.Distance);
                eventData.PointerCurrentRaycastTarget = hitEntity;
                eventData.PointerPressRaycastTarget = hitEntity;
                state.PressedEntity = hitEntity ? hitEntity.Handle() : entt::null;
                state.PressedScreen = pointerScreen;
                state.PressedPosition = pointer;
                if (hitEntity)
                    DispatchPointerEvent(scene, hitEntity.Handle(), eventData, &IPointerDownHandler::OnPointerDown);
            }

            if (pointerUpEdge && state.PressedEntity != entt::null && state.PressedScreen == cameraScreen) {
                Entity pressEntity(state.PressedEntity, &scene);
                eventData.TargetScreen = state.PressedScreen;
                eventData.Position = pointer;
                eventData.PointerPressRaycastTarget = pressEntity;
                hitEntity = raycastFn(scene, cameraEntity, pointer, maxDistance, eventData.WorldPoint, eventData.Distance);
                eventData.PointerCurrentRaycastTarget = hitEntity;

                DispatchPointerEvent(scene, pressEntity.Handle(), eventData, &IPointerUpHandler::OnPointerUp);
                if (hitEntity && pressEntity.Handle() == hitEntity.Handle())
                    DispatchPointerEvent(scene, hitEntity.Handle(), eventData, &IPointerClickHandler::OnPointerClick);

                state.PressedEntity = entt::null;
            }
        }

    }

    void UpdatePhysicsRaycasterInteractions(Scene& scene) {
        glm::vec2 pointer = InputManager::GetPointerPosition();
        Screen pointerScreen = InputManager::GetPointerScreen();
        bool pointerDown = InputManager::GetPointerDown();

        static bool s_WasPointerDown = false;
        bool pointerDownEdge = pointerDown && !s_WasPointerDown;
        bool pointerUpEdge = InputManager::GetPointerUp();
        s_WasPointerDown = pointerDown;

        auto raycaster2DView = scene.Registry().view<CameraComponent, PhysicsRaycaster2DComponent>();
        for (auto handle : raycaster2DView) {
            auto& raycaster = raycaster2DView.get<PhysicsRaycaster2DComponent>(handle);
            Entity cameraEntity(handle, &scene);
            RaycasterPointerState state{
                raycaster.HoveredEntity,
                raycaster.PressedEntity,
                raycaster.PressedScreen,
                raycaster.PressedPosition,
            };
            ProcessRaycaster(scene, cameraEntity, raycaster2DView.get<CameraComponent>(handle).Screen, raycaster.Enabled, state,
                             RaycastHit2DFromCamera, 0.0f, pointer, pointerScreen, pointerDown, pointerDownEdge, pointerUpEdge);
            raycaster.HoveredEntity = state.HoveredEntity;
            raycaster.PressedEntity = state.PressedEntity;
            raycaster.PressedScreen = state.PressedScreen;
            raycaster.PressedPosition = state.PressedPosition;
        }

        auto raycaster3DView = scene.Registry().view<CameraComponent, PhysicsRaycaster3DComponent>();
        for (auto handle : raycaster3DView) {
            auto& raycaster = raycaster3DView.get<PhysicsRaycaster3DComponent>(handle);
            Entity cameraEntity(handle, &scene);
            RaycasterPointerState state{
                raycaster.HoveredEntity,
                raycaster.PressedEntity,
                raycaster.PressedScreen,
                raycaster.PressedPosition,
            };
            ProcessRaycaster(scene, cameraEntity, raycaster3DView.get<CameraComponent>(handle).Screen, raycaster.Enabled, state,
                             RaycastHit3DFromCamera, raycaster.MaxDistance, pointer, pointerScreen, pointerDown, pointerDownEdge, pointerUpEdge);
            raycaster.HoveredEntity = state.HoveredEntity;
            raycaster.PressedEntity = state.PressedEntity;
            raycaster.PressedScreen = state.PressedScreen;
            raycaster.PressedPosition = state.PressedPosition;
        }
    }

}
