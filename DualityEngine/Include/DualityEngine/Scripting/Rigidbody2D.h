#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/EngineServices.h"

namespace Duality {

    // Unity's Rigidbody2D component API -- velocity/AddForce on an entity's own
    // Rigidbody2DComponent (b2Body isn't header-only, so calls route through EngineServices).
    // Obtain from Behaviour::GetRigidbody2D(), not GetComponent (the ECS component is
    // Rigidbody2DComponent and holds only authored/runtime bookkeeping fields).
    class Rigidbody2D {
    public:
        Rigidbody2D(Entity entity, const EngineServices* services) : m_Entity(entity), m_Services(services) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<Rigidbody2DComponent>(); }

        glm::vec2 GetVelocity() const {
            if (!m_Services || !m_Entity)
                return { 0.0f, 0.0f };
            float x = 0.0f, y = 0.0f;
            m_Services->GetVelocity2D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), &x, &y);
            return { x, y };
        }

        void SetVelocity(const glm::vec2& velocity) {
            if (m_Services && m_Entity)
                m_Services->SetVelocity2D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), velocity.x, velocity.y);
        }

        void AddForce(const glm::vec2& force) {
            if (m_Services && m_Entity)
                m_Services->AddForce2D(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), force.x, force.y);
        }

        BodyType GetBodyType() const {
            return *this ? m_Entity.GetComponent<Rigidbody2DComponent>().Type : BodyType::Dynamic;
        }
        void SetBodyType(BodyType type) {
            if (*this)
                m_Entity.GetComponent<Rigidbody2DComponent>().Type = type;
        }

        bool GetFixedRotation() const {
            return *this ? m_Entity.GetComponent<Rigidbody2DComponent>().FixedRotation : false;
        }
        void SetFixedRotation(bool fixedRotation) {
            if (*this)
                m_Entity.GetComponent<Rigidbody2DComponent>().FixedRotation = fixedRotation;
        }

    private:
        Entity m_Entity;
        const EngineServices* m_Services = nullptr;
    };

}
