#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // Unity's SpriteRenderer -- Color/Size/Texture on SpriteRendererComponent.
    class SpriteRenderer {
    public:
        explicit SpriteRenderer(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<SpriteRendererComponent>(); }

        glm::vec4 GetColor() const {
            return m_Entity ? m_Entity.GetComponent<SpriteRendererComponent>().Color : glm::vec4{ 1.0f };
        }
        void SetColor(const glm::vec4& color) {
            if (*this)
                m_Entity.GetComponent<SpriteRendererComponent>().Color = color;
        }

        glm::vec2 GetSize() const {
            return m_Entity ? m_Entity.GetComponent<SpriteRendererComponent>().Size : glm::vec2{};
        }
        void SetSize(const glm::vec2& size) {
            if (*this)
                m_Entity.GetComponent<SpriteRendererComponent>().Size = size;
        }

        AssetRef GetTexture() const {
            return m_Entity ? m_Entity.GetComponent<SpriteRendererComponent>().Texture : AssetRef{};
        }
        void SetTexture(const AssetRef& texture) {
            if (*this)
                m_Entity.GetComponent<SpriteRendererComponent>().Texture = texture;
        }
        void SetTextureGuid(const std::string& guid) {
            if (*this)
                m_Entity.GetComponent<SpriteRendererComponent>().Texture.Guid = guid;
        }

    private:
        Entity m_Entity;
    };

}
