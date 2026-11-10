#include "DualityEngine/Asset/MeshReference.h"

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"

namespace Duality::MeshReference {

    const std::vector<MeshVertex>* Vertices(const AssetRef& mesh) {
        if (mesh.Guid.empty())
            return nullptr;
        MeshPrimitive primitive;
        if (BuiltinMeshes::TryGetPrimitive(mesh, primitive))
            return &GetPrimitiveMesh(primitive);
        const std::string path = AssetDatabase::ResolvePath(mesh.Guid);
        if (path.empty())
            return nullptr;
        const MeshData& data = MeshLoader::Load(path);
        return data.Vertices.empty() ? nullptr : &data.Vertices;
    }

    float BoundingRadius(const AssetRef& mesh) {
        MeshPrimitive primitive;
        if (BuiltinMeshes::TryGetPrimitive(mesh, primitive))
            return BuiltinMeshes::BoundingRadius(primitive);
        if (!mesh.Guid.empty()) {
            const std::string path = AssetDatabase::ResolvePath(mesh.Guid);
            if (!path.empty()) {
                const float radius = MeshLoader::Load(path).BoundingRadius;
                if (radius > 0.0001f)
                    return radius;
            }
        }
        return BuiltinMeshes::BoundingRadius(MeshPrimitive::Cube);
    }

}
