#pragma once

#include <vector>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/MeshFilter.h"

namespace Duality {

    // Unity's MeshRenderer for C++ scripts (materials, runtime material color). The mesh itself
    // belongs to the MeshFilter (Scripting/MeshFilter.h).
    class MeshRenderer {
    public:
        explicit MeshRenderer(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<MeshRendererComponent>(); }

        const std::vector<AssetRef>& GetMaterials() const {
            static const std::vector<AssetRef> empty;
            return *this ? m_Entity.GetComponent<MeshRendererComponent>().Materials : empty;
        }
        void SetMaterials(const std::vector<AssetRef>& materials) {
            if (*this)
                m_Entity.GetComponent<MeshRendererComponent>().Materials = materials;
        }

        // A per-renderer, runtime-only override of the assigned material's Color property.
        // It preserves that material's Texture and ShadingMode, never writes its .mat file, and
        // is safe for desktop hot-reload scripts because it only touches ECS component data.
        void SetMaterialColor(const glm::vec4& color) {
            if (*this) {
                auto& component = m_Entity.GetComponent<MeshRendererComponent>();
                component.RuntimeMaterialColor = color;
                component.HasRuntimeMaterialColor = true;
            }
        }

        void ClearMaterialColor() {
            if (*this)
                m_Entity.GetComponent<MeshRendererComponent>().HasRuntimeMaterialColor = false;
        }

        bool HasMaterialColor() const {
            return *this && m_Entity.GetComponent<MeshRendererComponent>().HasRuntimeMaterialColor;
        }

        glm::vec4 GetMaterialColor() const {
            return HasMaterialColor()
                ? m_Entity.GetComponent<MeshRendererComponent>().RuntimeMaterialColor
                : glm::vec4(1.0f);
        }

        // --- Compatibility with scripts written before the MeshFilter split -----------------
        // The mesh moved to the MeshFilter component; these forward to it (setters add a
        // MeshFilter when the entity has none). Prefer MeshFilter(entity) in new code.
        MeshPrimitive GetPrimitive() const {
            MeshPrimitive primitive = MeshPrimitive::Cube;
            MeshFilter(m_Entity).IsBuiltinMesh(&primitive);
            return primitive;
        }
        void SetPrimitive(MeshPrimitive primitive) { SetMesh(BuiltinMeshes::Ref(primitive)); }
        AssetRef GetMesh() const { return MeshFilter(m_Entity).GetMesh(); }
        void SetMesh(const AssetRef& mesh) {
            if (!*this)
                return;
            if (!m_Entity.HasComponent<MeshFilterComponent>())
                m_Entity.AddComponent<MeshFilterComponent>();
            m_Entity.GetComponent<MeshFilterComponent>().Mesh = mesh;
        }

    private:
        Entity m_Entity;
    };

}
