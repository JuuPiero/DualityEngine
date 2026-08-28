#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // Unified 3D collider helpers -- BoxCollider3D or SphereCollider3D.
    class Collider3D {
    public:
        explicit Collider3D(Entity entity) : m_Entity(entity) {}

        operator bool() const {
            return m_Entity && (m_Entity.HasComponent<BoxCollider3DComponent>() || m_Entity.HasComponent<SphereCollider3DComponent>());
        }
        bool IsBox() const { return m_Entity && m_Entity.HasComponent<BoxCollider3DComponent>(); }
        bool IsSphere() const { return m_Entity && m_Entity.HasComponent<SphereCollider3DComponent>(); }

        bool IsTrigger() const {
            if (IsBox())
                return m_Entity.GetComponent<BoxCollider3DComponent>().IsTrigger;
            if (IsSphere())
                return m_Entity.GetComponent<SphereCollider3DComponent>().IsTrigger;
            return false;
        }
        void SetTrigger(bool trigger) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider3DComponent>().IsTrigger = trigger;
            else if (IsSphere())
                m_Entity.GetComponent<SphereCollider3DComponent>().IsTrigger = trigger;
        }

        glm::vec3 GetOffset() const {
            if (IsBox())
                return m_Entity.GetComponent<BoxCollider3DComponent>().Offset;
            if (IsSphere())
                return m_Entity.GetComponent<SphereCollider3DComponent>().Offset;
            return {};
        }
        void SetOffset(const glm::vec3& offset) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider3DComponent>().Offset = offset;
            else if (IsSphere())
                m_Entity.GetComponent<SphereCollider3DComponent>().Offset = offset;
        }

        glm::vec3 GetBoxSize() const {
            return IsBox() ? m_Entity.GetComponent<BoxCollider3DComponent>().Size : glm::vec3{};
        }
        void SetBoxSize(const glm::vec3& halfExtents) {
            if (IsBox())
                m_Entity.GetComponent<BoxCollider3DComponent>().Size = halfExtents;
        }

        float GetRadius() const {
            return IsSphere() ? m_Entity.GetComponent<SphereCollider3DComponent>().Radius : 0.0f;
        }
        void SetRadius(float radius) {
            if (IsSphere())
                m_Entity.GetComponent<SphereCollider3DComponent>().Radius = radius;
        }

    private:
        Entity m_Entity;
    };

}
