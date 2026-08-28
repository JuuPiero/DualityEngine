#pragma once

#include <vector>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/MeshPrimitive.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

  // MeshRendererComponent helpers.
    class MeshRenderer {
    public:
        explicit MeshRenderer(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<MeshRendererComponent>(); }

        MeshPrimitive GetPrimitive() const {
            return m_Entity ? m_Entity.GetComponent<MeshRendererComponent>().Primitive : MeshPrimitive::Cube;
        }
        void SetPrimitive(MeshPrimitive primitive) {
            if (*this)
                m_Entity.GetComponent<MeshRendererComponent>().Primitive = primitive;
        }

        AssetRef GetMesh() const {
            return m_Entity ? m_Entity.GetComponent<MeshRendererComponent>().Mesh : AssetRef{};
        }
        void SetMesh(const AssetRef& mesh) {
            if (*this)
                m_Entity.GetComponent<MeshRendererComponent>().Mesh = mesh;
        }

        const std::vector<AssetRef>& GetMaterials() const {
            static const std::vector<AssetRef> empty;
            return m_Entity ? m_Entity.GetComponent<MeshRendererComponent>().Materials : empty;
        }
        void SetMaterials(const std::vector<AssetRef>& materials) {
            if (*this)
                m_Entity.GetComponent<MeshRendererComponent>().Materials = materials;
        }

    private:
        Entity m_Entity;
    };

}
