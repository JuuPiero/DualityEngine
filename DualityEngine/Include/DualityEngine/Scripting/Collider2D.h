#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // Unified 2D collider helpers -- works with BoxCollider2D or CircleCollider2D on the entity.
    class Collider2D {
    public:
        explicit Collider2D(Entity entity) : m_Entity(entity) {}

        operator bool() const {
            return m_Entity && (m_Entity.HasComponent<BoxCollider2DComponent>() || m_Entity.HasComponent<CircleCollider2DComponent>());
        }
        bool IsBox() const { return m_Entity && m_Entity.HasComponent<BoxCollider2DComponent>(); }
        bool IsCircle() const { return m_Entity && m_Entity.HasComponent<CircleCollider2DComponent>(); }

        bool IsTrigger() const {
            if (IsBox())
                return m_Entity.GetComponent<BoxCollider2DComponent>().IsTrigger;
            if (IsCircle())
                return m_Entity.GetComponent<CircleCollider2DComponent>().IsTrigger;
            return false;
        }
        void SetTrigger(bool trigger) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider2DComponent>().IsTrigger = trigger;
            else if (IsCircle())
                m_Entity.GetComponent<CircleCollider2DComponent>().IsTrigger = trigger;
        }

        glm::vec2 GetOffset() const {
            if (IsBox())
                return m_Entity.GetComponent<BoxCollider2DComponent>().Offset;
            if (IsCircle())
                return m_Entity.GetComponent<CircleCollider2DComponent>().Offset;
            return {};
        }
        void SetOffset(const glm::vec2& offset) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider2DComponent>().Offset = offset;
            else if (IsCircle())
                m_Entity.GetComponent<CircleCollider2DComponent>().Offset = offset;
        }

        glm::vec2 GetBoxSize() const {
            return IsBox() ? m_Entity.GetComponent<BoxCollider2DComponent>().Size : glm::vec2{};
        }
        void SetBoxSize(const glm::vec2& halfExtents) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider2DComponent>().Size = halfExtents;
        }

        float GetRadius() const {
            return IsCircle() ? m_Entity.GetComponent<CircleCollider2DComponent>().Radius : 0.0f;
        }
        void SetRadius(float radius) {
            if (IsCircle())
                m_Entity.GetComponent<CircleCollider2DComponent>().Radius = radius;
        }

        float GetFriction() const {
            if (IsBox())
                return m_Entity.GetComponent<BoxCollider2DComponent>().Friction;
            if (IsCircle())
                return m_Entity.GetComponent<CircleCollider2DComponent>().Friction;
            return 0.0f;
        }
        void SetFriction(float friction) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider2DComponent>().Friction = friction;
            else if (IsCircle())
                m_Entity.GetComponent<CircleCollider2DComponent>().Friction = friction;
        }

        float GetRestitution() const {
            if (IsBox())
                return m_Entity.GetComponent<BoxCollider2DComponent>().Restitution;
            if (IsCircle())
                return m_Entity.GetComponent<CircleCollider2DComponent>().Restitution;
            return 0.0f;
        }
        void SetRestitution(float restitution) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider2DComponent>().Restitution = restitution;
            else if (IsCircle())
                m_Entity.GetComponent<CircleCollider2DComponent>().Restitution = restitution;
        }

    private:
        Entity m_Entity;
    };

}
