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

    struct ControllerColliderHit; // Scene/Components/Physics3D.h

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
    // which Scene sets before every lifecycle/collision callback. Use ScriptScene::LoadScene
    // for scene transitions so desktop GameScripts DLL calls reach the host-owned queue.
    class Behaviour {
    public:
        virtual ~Behaviour() = default;

        virtual void OnCreate() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnDestroy() {}

        // Unity's LateUpdate -- every frame after EVERY script's OnUpdate has run (camera
        // follow, reading positions other scripts just wrote). Same active/enabled gating as
        // OnUpdate.
        virtual void OnLateUpdate(float deltaTime) {}
        // Unity's FixedUpdate -- once per fixed 1/60s physics substep (0..N times per frame),
        // right before that substep is simulated, so forces/velocities set here are integrated
        // by it. Only for scripts already enabled (OnEnable fired), matching Unity's
        // Awake -> OnEnable -> Start -> FixedUpdate order.
        virtual void OnFixedUpdate(float fixedDeltaTime) {}

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

        // Unity's OnJointBreak2D / OnJointBreak -- sent to the scripts of the entity whose joint
        // exceeded its Break Force / Break Torque, right after the fixed step that broke it.
        // `joint` is that entity, `breakForce` the force (or torque) that broke it. The joint
        // component is removed right after the callbacks, like Unity destroying the Joint.
        virtual void OnJointBreak2D(Entity joint, float breakForce) {}
        virtual void OnJointBreak(Entity joint, float breakForce) {}
        // Unity's OnControllerColliderHit -- sent during CharacterController Move/SimpleMove
        // for every collider the capsule hit while moving (see Scripting/CharacterController.h).
        virtual void OnControllerColliderHit(const ControllerColliderHit& hit) {}

        template<typename T>
        T& GetComponent() { return m_Entity.GetComponent<T>(); }

        // Prefer this for optional components and EntityRef targets. It makes
        // ordinary authoring mistakes recoverable instead of an EnTT assertion.
        template<typename T>
        T* TryGetComponent() { return m_Entity.TryGetComponent<T>(); }
        template<typename T>
        const T* TryGetComponent() const { return m_Entity.TryGetComponent<T>(); }

        // Hierarchy lookups mirror Unity's MonoBehaviour helpers. They include this Entity by
        // default, traverse children depth-first / parents nearest-first, and return borrowed
        // component pointers valid until the next structural ECS change.
        template<typename T>
        T* GetComponentInChildren(bool includeSelf = true) { return FindComponentInChildren<T>(m_Entity, includeSelf); }
        template<typename T>
        const T* GetComponentInChildren(bool includeSelf = true) const { return FindComponentInChildrenConst<T>(m_Entity, includeSelf); }
        template<typename T>
        std::vector<T*> GetComponentsInChildren(bool includeSelf = true) {
            std::vector<T*> result;
            CollectComponentsInChildren<T>(m_Entity, includeSelf, result);
            return result;
        }
        template<typename T>
        std::vector<const T*> GetComponentsInChildren(bool includeSelf = true) const {
            std::vector<const T*> result;
            CollectComponentsInChildrenConst<T>(m_Entity, includeSelf, result);
            return result;
        }

        template<typename T>
        T* GetComponentInParent(bool includeSelf = true) {
            for (Entity current = includeSelf ? m_Entity : GetParent(); current; current = GetParentOf(current))
                if (T* component = current.TryGetComponent<T>()) return component;
            return nullptr;
        }
        template<typename T>
        const T* GetComponentInParent(bool includeSelf = true) const {
            for (Entity current = includeSelf ? m_Entity : GetParent(); current; current = GetParentOf(current)) {
                const Entity constCurrent = current;
                if (const T* component = constCurrent.TryGetComponent<T>()) return component;
            }
            return nullptr;
        }
        template<typename T>
        std::vector<T*> GetComponentsInParent(bool includeSelf = true) {
            std::vector<T*> result;
            for (Entity current = includeSelf ? m_Entity : GetParent(); current; current = GetParentOf(current))
                if (T* component = current.TryGetComponent<T>()) result.push_back(component);
            return result;
        }
        template<typename T>
        std::vector<const T*> GetComponentsInParent(bool includeSelf = true) const {
            std::vector<const T*> result;
            for (Entity current = includeSelf ? m_Entity : GetParent(); current; current = GetParentOf(current)) {
                const Entity constCurrent = current;
                if (const T* component = constCurrent.TryGetComponent<T>()) result.push_back(component);
            }
            return result;
        }

        Entity GetParent() const { return GetParentOf(m_Entity); }
        std::vector<Entity> GetChildren() const { return GetChildrenOf(m_Entity); }
        void ClearChildren() {
            const EngineServices* services = ScriptContext::Services();
            if (m_Entity && services && services->ClearEntityChildren)
                services->ClearEntityChildren(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()));
        }

        Entity GetEntity() const { return m_Entity; }
        bool IsEntityValid() const { return m_Entity.IsValid(); }

        // Transform is the one universal component wrapper: every Entity owns a Transform.
        // Other component wrappers are deliberately explicit, e.g.
        // `SpriteRenderer(GetEntity())` or `Rigidbody2D(GetEntity())`. Behaviour stays a
        // small lifecycle base class and does not pull render, physics, UI, or audio APIs into
        // every gameplay translation unit.
        Transform GetTransform() const { return Transform(m_Entity); }

        // Cross-language script access -- works the same whether `className` is another C++
        // Behaviour or a TypeScript class (Scripting/JS): the first live script of that class on
        // `entity` (default: this script's own entity). Fields are the Inspector fields
        // (DUALITY_PROPERTY / TypeScript @field), read/written by name as FieldValue.
        Behaviour* FindScript(const std::string& className, Entity entity = Entity{}) const {
            const EngineServices* services = ScriptContext::Services();
            Entity target = entity ? entity : m_Entity;
            if (!target || !services || !services->FindScript)
                return nullptr;
            return static_cast<Behaviour*>(services->FindScript(target.GetScene(), static_cast<unsigned int>(target.Handle()), className.c_str()));
        }
        bool GetScriptField(const std::string& className, const std::string& fieldName, FieldValue& outValue, Entity entity = Entity{}) const {
            const EngineServices* services = ScriptContext::Services();
            Behaviour* script = FindScript(className, entity);
            return script && services->GetScriptField &&
                services->GetScriptField(script, className.c_str(), fieldName.c_str(), &outValue);
        }
        template<typename T>
        bool GetScriptField(const std::string& className, const std::string& fieldName, T& outValue, Entity entity = Entity{}) const {
            FieldValue value;
            if (!GetScriptField(className, fieldName, value, entity))
                return false;
            if (const T* typed = std::get_if<T>(&value)) {
                outValue = *typed;
                return true;
            }
            return false;
        }
        bool SetScriptField(const std::string& className, const std::string& fieldName, const FieldValue& value, Entity entity = Entity{}) {
            const EngineServices* services = ScriptContext::Services();
            Behaviour* script = FindScript(className, entity);
            return script && services->SetScriptField &&
                services->SetScriptField(script, className.c_str(), fieldName.c_str(), &value);
        }
        // Unity's SendMessage for TypeScript scripts: calls `methodName()` on that script.
        bool SendScriptMessage(const std::string& className, const std::string& methodName, Entity entity = Entity{}) {
            const EngineServices* services = ScriptContext::Services();
            Behaviour* script = FindScript(className, entity);
            return script && services->CallScriptMethod && services->CallScriptMethod(script, methodName.c_str());
        }

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
        Entity GetParentOf(Entity entity) const {
            const EngineServices* services = ScriptContext::Services();
            unsigned int handle = 0;
            if (!entity || !services || !services->GetParentEntity ||
                !services->GetParentEntity(entity.GetScene(), static_cast<unsigned int>(entity.Handle()), &handle))
                return {};
            return Entity(static_cast<entt::entity>(handle), entity.GetScene());
        }

        std::vector<Entity> GetChildrenOf(Entity entity) const {
            std::vector<Entity> result;
            const EngineServices* services = ScriptContext::Services();
            if (!entity || !services || !services->GetChildCount || !services->GetChildAt)
                return result;
            const int count = services->GetChildCount(entity.GetScene(), static_cast<unsigned int>(entity.Handle()));
            result.reserve(count > 0 ? static_cast<size_t>(count) : 0);
            for (int index = 0; index < count; ++index) {
                unsigned int handle = 0;
                if (services->GetChildAt(entity.GetScene(), static_cast<unsigned int>(entity.Handle()), index, &handle))
                    result.emplace_back(static_cast<entt::entity>(handle), entity.GetScene());
            }
            return result;
        }

        template<typename T>
        T* FindComponentInChildren(Entity entity, bool includeSelf) {
            if (!entity) return nullptr;
            if (includeSelf && entity.TryGetComponent<T>()) return entity.TryGetComponent<T>();
            for (Entity child : GetChildrenOf(entity))
                if (T* component = FindComponentInChildren<T>(child, true)) return component;
            return nullptr;
        }

        template<typename T>
        const T* FindComponentInChildrenConst(Entity entity, bool includeSelf) const {
            if (!entity) return nullptr;
            const Entity constEntity = entity;
            if (includeSelf && constEntity.TryGetComponent<T>()) return constEntity.TryGetComponent<T>();
            for (Entity child : GetChildrenOf(entity))
                if (const T* component = FindComponentInChildrenConst<T>(child, true)) return component;
            return nullptr;
        }

        template<typename T>
        void CollectComponentsInChildren(Entity entity, bool includeSelf, std::vector<T*>& result) {
            if (!entity) return;
            if (includeSelf && entity.TryGetComponent<T>()) result.push_back(entity.TryGetComponent<T>());
            for (Entity child : GetChildrenOf(entity)) CollectComponentsInChildren<T>(child, true, result);
        }

        template<typename T>
        void CollectComponentsInChildrenConst(Entity entity, bool includeSelf, std::vector<const T*>& result) const {
            if (!entity) return;
            const Entity constEntity = entity;
            if (includeSelf && constEntity.TryGetComponent<T>()) result.push_back(constEntity.TryGetComponent<T>());
            for (Entity child : GetChildrenOf(entity)) CollectComponentsInChildrenConst<T>(child, true, result);
        }

        Entity m_Entity;
        // Points at the owning ScriptInstance::Enabled for the lifetime of this instance
        // (set by Scene::OnRuntimeStart). Null outside Play.
        bool* m_Enabled = nullptr;
        friend class Scene;
    };

}
