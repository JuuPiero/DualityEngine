#pragma once

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Resolved render layer (walks parent chain) plus direct LayerComponent access.
    class EntityLayer {
    public:
        explicit EntityLayer(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity; }

        Layer GetLayer() const {
            if (!m_Entity || !m_Entity.GetScene())
                return Layer::Default;
            return m_Entity.GetScene()->ResolveEntityLayer(m_Entity);
        }

        bool HasLayerComponent() const {
            return m_Entity && m_Entity.HasComponent<LayerComponent>();
        }

        Layer GetComponentLayer() const {
            return HasLayerComponent() ? m_Entity.GetComponent<LayerComponent>().Value : Layer::Default;
        }
        void SetComponentLayer(Layer layer) {
            if (!m_Entity)
                return;
            if (!m_Entity.HasComponent<LayerComponent>())
                m_Entity.AddComponent<LayerComponent>();
            m_Entity.GetComponent<LayerComponent>().Value = layer;
        }

    private:
        Entity m_Entity;
    };

}
