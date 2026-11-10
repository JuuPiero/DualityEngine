#pragma once

#include <vector>

#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"

namespace Duality {

    // One place that turns a Mesh reference (MeshFilter.Mesh, MeshCollider.Mesh) into data:
    // a built-in mesh ("builtin:Cube", see Renderer/BuiltinMeshes.h) or an imported mesh /
    // model guid loaded through MeshLoader. Used by rendering bounds, editor picking and the
    // Mesh Collider so they can never disagree about which mesh an entity has.
    namespace MeshReference {

        // CPU vertices (flat triangle list) of the referenced mesh; nullptr when the reference
        // is empty, unresolved or the imported mesh has no vertices.
        const std::vector<MeshVertex>* Vertices(const AssetRef& mesh);

        // Bounding-sphere radius of the mesh before Transform scale. Imported meshes whose
        // bounds are unknown use the unit cube's radius (what a primitive fallback used before).
        float BoundingRadius(const AssetRef& mesh);

    }

}
