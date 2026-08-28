#pragma once

#include "DualityEngine/Reflection/PropertyMacros.h" // brings in DUALITY_PROPERTY()/DUALITY_PROPERTIES_AUTO() for scripts
#include "DualityEngine/Scene/ActiveComponent.h"
#include "DualityEngine/Scene/PointerEventHandlers.h"
#include "DualityEngine/Scene/Scene.h" // Entity's GetComponent<T>/etc. templates are *defined*
                                       // here (after Scene is complete), not in Entity.h -- see
                                       // the same note in Reflection/TypeRegistry.h.
#include "DualityEngine/Scripting/AudioSource.h"
#include "DualityEngine/Scripting/Camera.h"
#include "DualityEngine/Scripting/Collider2D.h"
#include "DualityEngine/Scripting/Collider3D.h"
#include "DualityEngine/Scripting/EngineServices.h"
#include "DualityEngine/Scripting/EntityLayer.h"
#include "DualityEngine/Scripting/MeshRenderer.h"
#include "DualityEngine/Scripting/Rigidbody2D.h"
#include "DualityEngine/Scripting/Rigidbody3D.h"
#include "DualityEngine/Scripting/SpriteFlipbook.h"
#include "DualityEngine/Scripting/SpriteRenderer.h"
#include "DualityEngine/Scripting/Transform.h"
#include "DualityEngine/Scripting/UIWidgets.h"

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
    // live in the Scripting/ headers (ScriptInput, ScriptAudio, ScriptPhysics2D/3D,
    // ScriptDebug, ScriptScene) -- Unity-style static APIs bound via ScriptContext,
    // which Scene sets before every lifecycle/collision callback. Scene loading uses
    // SceneManager::RequestLoadScene directly (header-only, no EngineServices needed).
    class Behaviour : virtual public IPointerEnterHandler,
                      virtual public IPointerExitHandler,
                      virtual public IPointerDownHandler,
                      virtual public IPointerUpHandler,
                      virtual public IPointerClickHandler {
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

        // World-space pointer callbacks -- fired by UpdatePhysicsRaycasterInteractions when an
        // entity's collider is hit through a PhysicsRaycaster2D/3DComponent on the active camera.
        // Empty by default; override whichever handlers you need (same role as Unity's
        // IPointerDownHandler / IPointerClickHandler / ... on MonoBehaviour).
        void OnPointerEnter(PointerEventData& eventData) override {}
        void OnPointerExit(PointerEventData& eventData) override {}
        void OnPointerDown(PointerEventData& eventData) override {}
        void OnPointerUp(PointerEventData& eventData) override {}
        void OnPointerClick(PointerEventData& eventData) override {}

        template<typename T>
        T& GetComponent() { return m_Entity.GetComponent<T>(); }

        Entity GetEntity() const { return m_Entity; }

        // Unity-style component wrappers -- see Scripting/*.h for each API.
        Transform GetTransform() const { return Transform(m_Entity); }
        SpriteRenderer GetSpriteRenderer() const { return SpriteRenderer(m_Entity); }
        SpriteFlipbook GetSpriteFlipbook() const { return SpriteFlipbook(m_Entity); }
        MeshRenderer GetMeshRenderer() const { return MeshRenderer(m_Entity); }
        Camera GetCamera() const { return Camera(m_Entity); }
        Collider2D GetCollider2D() const { return Collider2D(m_Entity); }
        Collider3D GetCollider3D() const { return Collider3D(m_Entity); }
        EntityLayer GetEntityLayer() const { return EntityLayer(m_Entity); }
        UIButton GetUIButton() const { return UIButton(m_Entity); }
        UIRect GetUIRect() const { return UIRect(m_Entity); }
        UIImage GetUIImage() const { return UIImage(m_Entity); }
        UIText GetUIText() const { return UIText(m_Entity); }

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
        void SetActive(bool active) { GetComponent<ActiveComponent>().Active = active; }
        // Not const: matches GetComponent<T>() above, which EnTT's non-const
        // registry access requires -- entt::registry::get<T> has no const overload
        // reachable through Entity's own (also non-const) GetComponent<T>().
        bool IsActive() { return GetComponent<ActiveComponent>().Active; }

        // Unity's MonoBehaviour.enabled -- toggles THIS script slot only (ScriptInstance::
        // Enabled), not the whole entity. Wired by Scene at Play start via m_Enabled;
        // safe no-op before then / outside Play.
        void SetEnabled(bool enabled) { if (m_Enabled) *m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled ? *m_Enabled : true; }

        // Unity's Rigidbody2D/Rigidbody velocity/AddForce -- see Scripting/Rigidbody2D.h and
        // Scripting/Rigidbody3D.h for the actual API (routes through m_Services because
        // b2Body/btRigidBody aren't header-only types GameScripts can call into directly).
        Rigidbody2D GetRigidbody2D() const { return Rigidbody2D(m_Entity, m_Services); }
        Rigidbody3D GetRigidbody3D() const { return Rigidbody3D(m_Entity, m_Services); }

        // Unity's AudioSource -- see Scripting/AudioSource.h (routes through m_Services).
        AudioSource GetAudioSource() const { return AudioSource(m_Entity, m_Services); }

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

        // Called by Scene right after creating this instance -- not for
        // scripts to call themselves.
        void SetEngineServices(const EngineServices* services) { m_Services = services; }

    private:
        Entity m_Entity;
        const EngineServices* m_Services = nullptr;
        // Points at the owning ScriptInstance::Enabled for the lifetime of this instance
        // (set by Scene::OnRuntimeStart). Null outside Play.
        bool* m_Enabled = nullptr;
        friend class Scene;
    };

}
