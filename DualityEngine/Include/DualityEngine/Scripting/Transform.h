#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // Unity's Transform -- local/world position, rotation, scale helpers on TransformComponent.
    // Every entity has TransformComponent; obtain via Behaviour::GetTransform().
    class Transform {
    public:
        explicit Transform(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<TransformComponent>(); }

        glm::vec3 GetLocalPosition() const {
            return m_Entity ? m_Entity.GetComponent<TransformComponent>().Translation : glm::vec3{};
        }
        void SetLocalPosition(const glm::vec3& position) {
            if (*this)
                m_Entity.GetComponent<TransformComponent>().Translation = position;
        }

        glm::vec2 GetLocalPosition2D() const {
            glm::vec3 p = GetLocalPosition();
            return { p.x, p.y };
        }
        void SetLocalPosition2D(const glm::vec2& position) {
            if (!*this)
                return;
            auto& t = m_Entity.GetComponent<TransformComponent>();
            t.Translation.x = position.x;
            t.Translation.y = position.y;
        }

        glm::vec3 GetWorldPosition() const {
            if (!m_Entity || !m_Entity.GetScene())
                return {};
            return m_Entity.GetScene()->GetWorldTransform(m_Entity).Translation;
        }
        glm::vec2 GetWorldPosition2D() const {
            glm::vec3 p = GetWorldPosition();
            return { p.x, p.y };
        }

        void Translate(const glm::vec3& delta) {
            if (*this)
                m_Entity.GetComponent<TransformComponent>().Translation += delta;
        }
        void Translate2D(const glm::vec2& delta) {
            if (!*this)
                return;
            auto& t = m_Entity.GetComponent<TransformComponent>();
            t.Translation.x += delta.x;
            t.Translation.y += delta.y;
        }

        glm::vec3 GetLocalRotation() const {
            return m_Entity ? m_Entity.GetComponent<TransformComponent>().Rotation : glm::vec3{};
        }
        void SetLocalRotation(const glm::vec3& rotation) {
            if (*this)
                m_Entity.GetComponent<TransformComponent>().Rotation = rotation;
        }

        float GetRotationZ() const { return GetLocalRotation().z; }
        void SetRotationZ(float degrees) {
            if (*this)
                m_Entity.GetComponent<TransformComponent>().Rotation.z = degrees;
        }
        float GetRotationY() const { return GetLocalRotation().y; }
        void SetRotationY(float degrees) {
            if (*this)
                m_Entity.GetComponent<TransformComponent>().Rotation.y = degrees;
        }

        glm::vec3 GetLocalScale() const {
            return m_Entity ? m_Entity.GetComponent<TransformComponent>().Scale : glm::vec3{ 1.0f };
        }
        void SetLocalScale(const glm::vec3& scale) {
            if (*this)
                m_Entity.GetComponent<TransformComponent>().Scale = scale;
        }
        glm::vec2 GetLocalScale2D() const {
            glm::vec3 s = GetLocalScale();
            return { s.x, s.y };
        }
        void SetLocalScale2D(const glm::vec2& scale) {
            if (!*this)
                return;
            auto& t = m_Entity.GetComponent<TransformComponent>();
            t.Scale.x = scale.x;
            t.Scale.y = scale.y;
        }

    private:
        Entity m_Entity;
    };

}
