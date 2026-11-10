#pragma once

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // Unity's MeshFilter for C++ scripts: `MeshFilter(GetEntity()).SetMesh(BuiltinMeshes::Ref(MeshPrimitive::Sphere))`.
    // The mesh is an AssetRef: an imported mesh/model guid or a built-in "builtin:<Name>".
    class MeshFilter {
    public:
        explicit MeshFilter(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<MeshFilterComponent>(); }

        AssetRef GetMesh() const {
            return *this ? m_Entity.GetComponent<MeshFilterComponent>().Mesh : AssetRef{};
        }
        void SetMesh(const AssetRef& mesh) {
            if (*this)
                m_Entity.GetComponent<MeshFilterComponent>().Mesh = mesh;
        }
        void SetMesh(MeshPrimitive builtinMesh) { SetMesh(BuiltinMeshes::Ref(builtinMesh)); }

        // True when the mesh is one of the built-in meshes (Cube, Sphere...).
        bool IsBuiltinMesh(MeshPrimitive* outPrimitive = nullptr) const {
            MeshPrimitive primitive;
            const bool builtin = BuiltinMeshes::TryGetPrimitive(GetMesh(), primitive);
            if (builtin && outPrimitive)
                *outPrimitive = primitive;
            return builtin;
        }

    private:
        Entity m_Entity;
    };

}
