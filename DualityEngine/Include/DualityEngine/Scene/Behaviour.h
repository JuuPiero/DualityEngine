#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Physics/RaycastHit.h"
#include "DualityEngine/Reflection/PropertyMacros.h" // brings in DUALITY_PROPERTIES() for scripts
#include "DualityEngine/Scene/ActiveComponent.h"
#include "DualityEngine/Scene/Scene.h" // Entity's GetComponent<T>/etc. templates are *defined*
                                       // here (after Scene is complete), not in Entity.h -- see
                                       // the same note in Reflection/TypeRegistry.h.
#include "DualityEngine/Scripting/EngineServices.h"

namespace Duality {

    // The MonoBehaviour-equivalent base class for gameplay code, written as
    // plain C++ (no embedded scripting language). Subclasses can declare
    // Inspector-editable public fields (Unity's [SerializeField]-equivalent)
    // via DUALITY_PROPERTIES (Reflection/PropertyMacros.h) -- Edit-mode
    // edits are stored on BehaviourComponent::PropertyOverrides and applied
    // onto the real instance right before OnCreate() at Play start (see
    // Scene::OnRuntimeStart). DLL hot-reload for fast desktop iteration is
    // still a later phase.
    class Behaviour {
    public:
        virtual ~Behaviour() = default;

        virtual void OnCreate() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnDestroy() {}

        // Fired by Scene::OnRuntimeUpdate when this entity's Scene::IsEffectivelyActive
        // result flips (OnEnable once when it becomes true, OnDisable once when it
        // becomes false) -- OnUpdate simply isn't called at all while inactive. Unlike
        // OnCreate/OnDestroy, these can fire many times across one Play session as a
        // script calls SetActive back and forth. OnCreate still always runs once at Play
        // start regardless of starting Active state (matches Unity's Awake-always-runs
        // rule); a still-active instance also gets one final OnDisable right before
        // OnDestroy at OnRuntimeStop.
        virtual void OnEnable() {}
        virtual void OnDisable() {}

        // Fired by Scene::OnRuntimeUpdate right after each physics world steps, once per
        // touching-pair transition this frame -- BOTH entities in a pair get the callback,
        // each passed the OTHER as `other` (Unity's own convention). A pair counts as a
        // trigger callback if EITHER collider involved has IsTrigger set, and a collision
        // callback only when NEITHER does -- never both for the same pair/frame. Works
        // identically for 2D (Box2D) and 3D (Bullet) colliders; `other` may itself be either
        // kind, there's no separate 2D/3D callback surface. No OnCollisionStay/
        // OnTriggerStay in this first pass (see ROADMAP.md) -- only the enter/exit edges.
        virtual void OnCollisionEnter(Entity other) {}
        virtual void OnCollisionExit(Entity other) {}
        virtual void OnTriggerEnter(Entity other) {}
        virtual void OnTriggerExit(Entity other) {}

        template<typename T>
        T& GetComponent() { return m_Entity.GetComponent<T>(); }

        Entity GetEntity() const { return m_Entity; }

        // Unity's GameObject.SetActive/activeSelf -- pure ECS-side (unlike the Input/
        // Audio/etc. methods below, this never needs to cross the GameScripts DLL
        // boundary to a different subsystem, so it goes straight through
        // ActiveComponent rather than EngineServices). Note this sets THIS entity's own
        // flag only -- see Scene::IsEffectivelyActive for the cascading parent-aware
        // check OnUpdate/rendering/physics actually use.
        void SetActive(bool active) { GetComponent<ActiveComponent>().Active = active; }
        // Not const: matches GetComponent<T>() above, which EnTT's non-const
        // registry access requires -- entt::registry::get<T> has no const overload
        // reachable through Entity's own (also non-const) GetComponent<T>().
        bool IsActive() { return GetComponent<ActiveComponent>().Active; }

        // Input, Unity-Input-Manager-style -- called bare from inside a
        // script's own OnCreate/OnUpdate (via this inherited method, not a
        // global Input:: static). Forwards through m_Services rather than
        // calling Duality::Input directly: on desktop, this Behaviour may
        // live inside a separately-compiled GameScripts.dll that doesn't
        // link DualityEngine's compiled lib at all (the same reason LogInfo/
        // LogWarn/LogError below route through m_Services too, instead of
        // calling Duality::Log directly), so m_Services (wired by Scene
        // right after creating this instance, see EngineServices.h) is the
        // only thing scripts can safely call across that boundary. Returns
        // false/0/{0,0} if m_Services hasn't been set yet.
        bool GetKey(KeyCode key) const { return m_Services && m_Services->GetKey(static_cast<int>(key)); }
        bool GetKeyDown(KeyCode key) const { return m_Services && m_Services->GetKeyDown(static_cast<int>(key)); }
        bool GetKeyUp(KeyCode key) const { return m_Services && m_Services->GetKeyUp(static_cast<int>(key)); }
        float GetAxis(const std::string& axisName) const { return m_Services ? m_Services->GetAxis(axisName.c_str()) : 0.0f; }
        bool GetPointerDown() const { return m_Services && m_Services->GetPointerDown(); }
        glm::vec2 GetPointerPosition() const {
            if (!m_Services)
                return { 0.0f, 0.0f };
            float x = 0.0f, y = 0.0f;
            m_Services->GetPointerPosition(&x, &y);
            return { x, y };
        }
        // Which screen GetPointerPosition() is local to -- Top (400x240) and Bottom (320x240)
        // share overlapping local pixel ranges, so this is the only way to tell which one a
        // touch/click actually landed on. Defaults to Screen::Top if m_Services isn't set yet.
        Screen GetPointerScreen() const { return m_Services ? static_cast<Screen>(m_Services->GetPointerScreen()) : Screen::Top; }

        // `assetGuid` is an AssetRef's Guid (e.g. an AssetRef field's
        // .Guid, or a value read from another component) -- resolved to a
        // real file path on the engine side of m_Services, same reasoning
        // as the Input methods above.
        void PlaySound(const std::string& assetGuid, bool loop = false) const {
            if (m_Services) m_Services->PlaySound(assetGuid.c_str(), loop);
        }
        void StopAllSounds() const { if (m_Services) m_Services->StopAllSounds(); }

        // Duality::Debug.Log-equivalent -- shows up in the Editor's Console panel exactly
        // like every other engine log line (Scene/ScriptEngine/BuildPipeline/etc.), same
        // DLL-boundary reasoning as the Input methods above.
        void LogInfo(const std::string& message) const { if (m_Services) m_Services->LogInfo(message.c_str()); }
        void LogWarn(const std::string& message) const { if (m_Services) m_Services->LogWarn(message.c_str()); }
        void LogError(const std::string& message) const { if (m_Services) m_Services->LogError(message.c_str()); }

        // Unity's SceneManager.LoadScene -- `assetsRelativePath` is a path relative to the
        // current project's Assets root (e.g. "Scenes/Level2.json"), resolved by whichever
        // real loop owns the Scene (see SceneManager.h's own comment on why this is
        // deferred rather than immediate). The swap actually happens between frames, not
        // synchronously when this returns -- code after this call in the SAME OnUpdate
        // still runs against the OLD scene.
        void LoadScene(const std::string& assetsRelativePath) const {
            if (m_Services) m_Services->RequestLoadScene(assetsRelativePath.c_str());
        }

        // Cross-screen entity lookup by name -- e.g. a script on a Top-screen entity
        // calling FindEntityInBottomScreen("Paddle") to reach an entity organized
        // under that screen's ScreenGroupComponent (see Components.h). Both entities
        // share the same Scene/Behaviour lifecycle either way -- "screen" here is
        // purely an organizational lookup filter, not a hard boundary. Returns an
        // empty Entity (falsy) if nothing matches or m_Services hasn't been set yet.
        Entity FindEntityInScreen(Screen screen, const std::string& name) const {
            if (!m_Services)
                return Entity{};
            unsigned int handle = 0;
            if (!m_Services->FindEntityInScreen(m_Entity.GetScene(), static_cast<int>(screen), name.c_str(), &handle))
                return Entity{};
            return Entity(static_cast<entt::entity>(handle), m_Entity.GetScene());
        }
        Entity FindEntityInTopScreen(const std::string& name) const { return FindEntityInScreen(Screen::Top, name); }
        Entity FindEntityInBottomScreen(const std::string& name) const { return FindEntityInScreen(Screen::Bottom, name); }

        // Converts an EntityRef field (see Reflection/Field.h) to a usable Entity in THIS
        // Behaviour's own Scene -- e.g. `Entity target = ResolveEntityRef(m_Target);` then
        // `target.GetComponent<Rigidbody2DComponent>()`. There's no separate "component
        // reference" field type: a script that needs one just holds an EntityRef and calls
        // GetComponent<T>() itself, same as Unity's own [SerializeField] GameObject fields.
        // Returns an empty (falsy) Entity if the ref is unset or this Behaviour has no Scene yet.
        Entity ResolveEntityRef(EntityRef ref) const {
            if (ref.Handle == EntityRef::Invalid || !m_Entity.GetScene())
                return Entity{};
            return Entity(static_cast<entt::entity>(ref.Handle), m_Entity.GetScene());
        }
        static EntityRef MakeEntityRef(Entity entity) {
            return entity ? EntityRef{ static_cast<uint32_t>(entity.Handle()) } : EntityRef{};
        }

        // Unity's Object.Instantiate -- spawns a new copy of the Prefab asset referenced
        // by `prefabAssetGuid` (an AssetRef's Guid) as a root entity in this Behaviour's
        // own Scene. Returns an empty Entity (falsy) if the guid doesn't resolve to a
        // loadable prefab or m_Services hasn't been set yet.
        Entity Instantiate(const std::string& prefabAssetGuid) const {
            if (!m_Services)
                return Entity{};
            unsigned int handle = 0;
            if (!m_Services->Instantiate(m_Entity.GetScene(), prefabAssetGuid.c_str(), &handle))
                return Entity{};
            return Entity(static_cast<entt::entity>(handle), m_Entity.GetScene());
        }

        // Unity's ScriptableObject data-asset lookup -- `assetGuid` is an AssetRef's Guid
        // pointing at a ".asset" file (see Asset/ScriptableObjectLoader.h). The caller
        // supplies the concrete GameScripts-side type T (e.g. LoadScriptableObject<GameSettings>
        // (guid)) since the engine side only ever hands back an opaque void* (same ABI-safety
        // reasoning as every other m_Services call). Returns nullptr if the guid is empty/
        // unresolved, the file doesn't parse, or its stored class isn't one GameScripts has
        // registered (e.g. GameScripts hasn't been (re)loaded yet) -- callers should treat a
        // null result the same as a missing AssetRef anywhere else in this engine, not an error.
        template<typename T>
        T* LoadScriptableObject(const std::string& assetGuid) const {
            if (!m_Services || assetGuid.empty())
                return nullptr;
            return static_cast<T*>(m_Services->LoadScriptableObject(assetGuid.c_str()));
        }

        // Unity's Rigidbody2D.velocity/Rigidbody.velocity and AddForce -- operates on THIS
        // entity's own Rigidbody2DComponent/Rigidbody3DComponent (whichever exists; there's no
        // GetComponent<Rigidbody2DComponent>().velocity the way Unity's own API reads, since
        // b2Body/btRigidBody aren't header-only types GameScripts can call into directly -- see
        // EngineServices.h's own comment). Returns a zero vector if this entity has no
        // Rigidbody of that dimension, or Play isn't running yet.
        glm::vec2 GetVelocity2D() const {
            if (!m_Services)
                return { 0.0f, 0.0f };
            float x = 0.0f, y = 0.0f;
            m_Services->GetVelocity2D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), &x, &y);
            return { x, y };
        }
        void SetVelocity2D(const glm::vec2& velocity) const {
            if (m_Services) m_Services->SetVelocity2D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), velocity.x, velocity.y);
        }
        void AddForce2D(const glm::vec2& force) const {
            if (m_Services) m_Services->AddForce2D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), force.x, force.y);
        }
        glm::vec3 GetVelocity3D() const {
            if (!m_Services)
                return { 0.0f, 0.0f, 0.0f };
            float x = 0.0f, y = 0.0f, z = 0.0f;
            m_Services->GetVelocity3D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), &x, &y, &z);
            return { x, y, z };
        }
        void SetVelocity3D(const glm::vec3& velocity) const {
            if (m_Services) m_Services->SetVelocity3D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), velocity.x, velocity.y, velocity.z);
        }
        void AddForce3D(const glm::vec3& force) const {
            if (m_Services) m_Services->AddForce3D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), force.x, force.y, force.z);
        }

        // Unity's Physics2D.Raycast/Physics.Raycast -- casts against this Scene's own live
        // Box2D/Bullet colliders (only valid while Play is running). Returns a falsy
        // RaycastHit2D/3D (check via `if (hit)`) if nothing was hit or Play isn't running.
        RaycastHit2D Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) const {
            RaycastHit2D hit;
            if (!m_Services)
                return hit;
            unsigned int handle = 0;
            float px = 0, py = 0, nx = 0, ny = 0, dist = 0;
            if (!m_Services->Raycast2D(m_Entity.GetScene(), origin.x, origin.y, direction.x, direction.y, maxDistance, &handle, &px, &py, &nx, &ny, &dist))
                return hit;
            hit.HitEntity = Entity(static_cast<entt::entity>(handle), m_Entity.GetScene());
            hit.Point = { px, py };
            hit.Normal = { nx, ny };
            hit.Distance = dist;
            return hit;
        }
        RaycastHit3D Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
            RaycastHit3D hit;
            if (!m_Services)
                return hit;
            unsigned int handle = 0;
            float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, dist = 0;
            if (!m_Services->Raycast3D(m_Entity.GetScene(), origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance,
                                        &handle, &px, &py, &pz, &nx, &ny, &nz, &dist))
                return hit;
            hit.HitEntity = Entity(static_cast<entt::entity>(handle), m_Entity.GetScene());
            hit.Point = { px, py, pz };
            hit.Normal = { nx, ny, nz };
            hit.Distance = dist;
            return hit;
        }

        // Converts a point in `screen`'s own local pixel space (e.g. GetPointerPosition(), with
        // `screen` = GetPointerScreen()) into a world-space ray from that screen's primary
        // camera -- feed the result straight into Raycast3D for click/touch-to-select gameplay.
        // Returns false (outOrigin/outDirection untouched) if that screen has no Perspective
        // primary camera, or Play isn't running yet.
        bool ScreenPointToRay3D(Screen screen, const glm::vec2& screenPoint, glm::vec3& outOrigin, glm::vec3& outDirection) const {
            if (!m_Services)
                return false;
            float ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0;
            if (!m_Services->ScreenPointToRay3D(m_Entity.GetScene(), static_cast<int>(screen), screenPoint.x, screenPoint.y, &ox, &oy, &oz, &dx, &dy, &dz))
                return false;
            outOrigin = { ox, oy, oz };
            outDirection = { dx, dy, dz };
            return true;
        }

        // Called by Scene right after creating this instance -- not for
        // scripts to call themselves.
        void SetEngineServices(const EngineServices* services) { m_Services = services; }

    private:
        Entity m_Entity;
        const EngineServices* m_Services = nullptr;
        friend class Scene;
    };

}
