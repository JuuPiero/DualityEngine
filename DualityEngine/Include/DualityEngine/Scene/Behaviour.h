#pragma once

#include "DualityEngine/Scene/Scene.h" // Entity's GetComponent<T>/etc. templates are *defined*
                                       // here (after Scene is complete), not in Entity.h -- see
                                       // the same note in Reflection/TypeRegistry.h.

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

        template<typename T>
        T& GetComponent() { return m_Entity.GetComponent<T>(); }

        Entity GetEntity() const { return m_Entity; }

    private:
        Entity m_Entity;
        friend class Scene;
    };

}
