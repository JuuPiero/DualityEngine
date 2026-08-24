#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Scene/ActiveComponent.h"
#include "DualityEngine/Scene/Scene.h" // Entity's GetComponent<T>/etc. templates are *defined*
                                       // here (after Scene is complete), not in Entity.h -- see
                                       // the same note in Reflection/TypeRegistry.h.
#include "DualityEngine/Scripting/EngineServices.h"

namespace Duality {

    // The MonoBehaviour-equivalent base class for gameplay code, written as
    // plain C++ (no embedded scripting language). A later phase adds
    // reflection-driven public fields (Inspector-editable, like Unity's
    // [SerializeField]) and DLL hot-reload for fast desktop iteration --
    // this first version only wires up the lifecycle itself.
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

        // Called by Scene right after creating this instance -- not for
        // scripts to call themselves.
        void SetEngineServices(const EngineServices* services) { m_Services = services; }

    private:
        Entity m_Entity;
        const EngineServices* m_Services = nullptr;
        friend class Scene;
    };

}
