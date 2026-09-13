#pragma once

#include "DualityEngine/Reflection/PropertyMacros.h" // brings in DUALITY_PROPERTY()/DUALITY_PROPERTIES_AUTO() for scripts
#include "DualityEngine/Scene/ActiveComponent.h"
#include "DualityEngine/Scene/Scene.h" // Entity's GetComponent<T>/etc. templates are *defined*
                                       // here (after Scene is complete), not in Entity.h -- see
                                       // the same note in Reflection/TypeRegistry.h.
#include "DualityEngine/Scripting/EngineServices.h"
#include "DualityEngine/Scripting/ScriptContext.h"
#include "DualityEngine/Scripting/Transform.h"

namespace Duality {

    // The MonoBehaviour-equivalent base class for gameplay code, written as
    // plain C++ (no embedded scripting language). Subclasses can declare
    // Inspector-editable public fields (Unity's [SerializeField]-equivalent)
    // via DUALITY_PROPERTY()/DUALITY_PROPERTIES_AUTO() (Reflection/PropertyMacros.h) --
    // Edit-mode edits are stored on the owning ScriptInstance::PropertyOverrides
    // (Components.h) and applied onto the real instance right before OnCreate()
    // at Play start (see Scene::OnRuntimeStart). DLL hot-reload for fast desktop
    // iteration is still a later phase.
    //
    // Engine services (Input, Audio, Raycast, Debug.Log, scene lookup, Instantiate, ...)
    // live in the Scripting/ headers (Input, ScriptAudio, ScriptPhysics2D/3D,
    // ScriptDebug, ScriptScene) -- Unity-style static APIs bound via ScriptContext,
    // which Scene sets before every lifecycle/collision callback. Scene loading uses
    // SceneManager::RequestLoadScene directly (header-only, no EngineServices needed).
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

        // Platform-neutral application lifecycle. On Nintendo 3DS these are
        // driven by APT (HOME/suspend/sleep/wake/exit); desktop hosts can map
        // their window focus lifecycle to the same callbacks later. Keep save
        // checkpoints and network reconnect logic here, never in a raw 3DS
        // service callback.
        virtual void OnApplicationFocus(bool focused) {}
        virtual void OnApplicationPause(bool paused) {}
        virtual void OnApplicationQuit() {}

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

        // Prefer this for optional components and EntityRef targets. It makes
        // ordinary authoring mistakes recoverable instead of an EnTT assertion.
        template<typename T>
        T* TryGetComponent() { return m_Entity.TryGetComponent<T>(); }
        template<typename T>
        const T* TryGetComponent() const { return m_Entity.TryGetComponent<T>(); }

        Entity GetEntity() const { return m_Entity; }
        bool IsEntityValid() const { return m_Entity.IsValid(); }

        // Transform is the one universal component wrapper: every Entity owns a Transform.
        // Other component wrappers are deliberately explicit, e.g.
        // `SpriteRenderer(GetEntity())` or `Rigidbody2D(GetEntity())`. Behaviour stays a
        // small lifecycle base class and does not pull render, physics, UI, or audio APIs into
        // every gameplay translation unit.
        Transform GetTransform() const { return Transform(m_Entity); }

        std::string GetName() const {
            return m_Entity && m_Entity.HasComponent<NameComponent>()
                ? m_Entity.GetComponent<NameComponent>().Name
                : std::string{};
        }
        void SetName(const std::string& name) {
            if (m_Entity && m_Entity.HasComponent<NameComponent>())
                m_Entity.GetComponent<NameComponent>().Name = name;
        }

        std::string GetTag() const {
            return m_Entity && m_Entity.HasComponent<TagComponent>()
                ? m_Entity.GetComponent<TagComponent>().Tag
                : std::string{};
        }
        void SetTag(const std::string& tag) {
            if (m_Entity && m_Entity.HasComponent<TagComponent>())
                m_Entity.GetComponent<TagComponent>().Tag = tag;
        }

        // Unity's GameObject.SetActive/activeSelf -- pure ECS-side (never needs to cross the
        // GameScripts DLL boundary to a different subsystem, so it goes straight through
        // ActiveComponent rather than EngineServices). Note this sets THIS entity's own
        // flag only -- see Scene::IsEffectivelyActive for the cascading parent-aware
        // check OnUpdate/rendering/physics actually use.
        void SetActive(bool active) {
            if (auto* component = TryGetComponent<ActiveComponent>())
                component->Active = active;
        }
        // Not const: matches GetComponent<T>() above, which EnTT's non-const
        // registry access requires -- entt::registry::get<T> has no const overload
        // reachable through Entity's own (also non-const) GetComponent<T>().
        bool IsActive() {
            const auto* component = TryGetComponent<ActiveComponent>();
            return component && component->Active;
        }

        // Unity's MonoBehaviour.enabled -- toggles THIS script slot only (ScriptInstance::
        // Enabled), not the whole entity. Wired by Scene at Play start via m_Enabled;
        // safe no-op before then / outside Play.
        void SetEnabled(bool enabled) { if (m_Enabled) *m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled ? *m_Enabled : true; }

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

        // Called by Scene right after creating this instance -- not for scripts to call
        // themselves. Also self-heals ScriptContext's own copy of the services pointer for
        // whichever binary THIS code actually executes from (see ScriptContext::EnsureBound's
        // own comment) -- on desktop, script subclasses live inside GameScripts.dll, a separate
        // binary from the engine/host that calls this method via a plain `Behaviour*`. Must
        // stay `virtual` for that self-heal to matter at all: a NON-virtual method called
        // through a base-class pointer is resolved at the CALL SITE's own compile time (i.e.
        // using Scene.cpp's/the host's own inlined copy, updating the HOST's copy of
        // ScriptContext's static storage, not GameScripts.dll's) -- virtual dispatch instead
        // goes through the concrete object's own vtable, which for a GameScripts-allocated
        // instance was built (and points into code compiled) inside GameScripts.dll itself, so
        // the call actually executes there.
        virtual void SetEngineServices(const EngineServices* services) {
            ScriptContext::EnsureBound(services);
        }

    private:
        Entity m_Entity;
        // Points at the owning ScriptInstance::Enabled for the lifetime of this instance
        // (set by Scene::OnRuntimeStart). Null outside Play.
        bool* m_Enabled = nullptr;
        friend class Scene;
    };

}
