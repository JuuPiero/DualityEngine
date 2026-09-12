#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity's Rigidbody component API -- velocity/AddForce on an entity's own
    // Rigidbody3DComponent (btRigidBody isn't header-only, so calls route through
    // ScriptContext's EngineServices bridge). Create explicitly with `Rigidbody3D(entity)`.
    class Rigidbody3D {
    public:
        explicit Rigidbody3D(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<Rigidbody3DComponent>(); }

        glm::vec3 GetVelocity() const {
            const EngineServices* services = ScriptContext::Services();
            if (!services || !m_Entity)
                return { 0.0f, 0.0f, 0.0f };
            float x = 0.0f, y = 0.0f, z = 0.0f;
            services->GetVelocity3D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), &x, &y, &z);
            return { x, y, z };
        }

        void SetVelocity(const glm::vec3& velocity) {
            if (const EngineServices* services = ScriptContext::Services(); services && m_Entity)
                services->SetVelocity3D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), velocity.x, velocity.y, velocity.z);
        }

        void AddForce(const glm::vec3& force) {
            if (const EngineServices* services = ScriptContext::Services(); services && m_Entity)
                services->AddForce3D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), force.x, force.y, force.z);
        }

        BodyType GetBodyType() const {
            return *this ? m_Entity.GetComponent<Rigidbody3DComponent>().Type : BodyType::Dynamic;
        }
        void SetBodyType(BodyType type) {
            if (*this)
                m_Entity.GetComponent<Rigidbody3DComponent>().Type = type;
        }

    private:
        Entity m_Entity;
    };

}
