#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "DualityEngine/Renderer/PrimitiveMeshes.h"

namespace Duality {

    struct MeshData {
        struct Bone { std::string Name; glm::mat4 Offset{ 1.0f }; };
        // Flat, non-indexed triangle list, matching PrimitiveMeshes' own convention -- empty if
        // the file didn't exist or failed to parse.
        std::vector<MeshVertex> Vertices;
        // Max distance from the origin across all vertices, for the Editor's Scene view pick
        // test (see ScenePanel.cpp) -- 0 if Vertices is empty.
        float BoundingRadius = 0.0f;

        // One contiguous (FirstVertex, VertexCount) range into Vertices above, non-indexed
        // (matching Vertices' own convention -- a submesh is just a slice of the same flat
        // triangle list, no separate index buffer). Always >= 1 entry once Vertices is
        // non-empty, even for a file with no usemtl/o/g groups at all -- that case just produces
        // one SubMesh spanning the whole range, expressing today's original single-material
        // behavior as the 1-submesh case of this same mechanism rather than a special case.
        struct SubMesh {
            uint32_t FirstVertex = 0;
            uint32_t VertexCount = 0;
            // Global MeshData::Bones indices used by this draw. Version-3 cooked meshes
            // remap each vertex's BoneIndices into this compact palette, keeping a PICA
            // draw within its uniform budget without discarding any triangle or bone.
            std::vector<uint16_t> BonePalette;
        };
        std::vector<SubMesh> SubMeshes;
        std::vector<Bone> Bones;
        // V2 stored global u32 ids before palette cooking existed. Keep those exact values only
        // for its CPU fallback; V3 rendering always uses compact MeshVertex local ids.
        std::vector<glm::uvec4> LegacyBoneIndices;
    };

    // Loads a Wavefront ".obj" file or a cooked ".dmesh" file into a flat MeshVertex list.
    // .dmesh is the compact, Assimp-free runtime result of importing FBX/glTF in the desktop
    // editor, so it is equally loadable by the desktop renderer and the 3DS player.
    // (positions/texcoords/normals/optional vertex colors/triangulated faces; materials and negative/relative indices
    // are not supported) chosen specifically because it has zero external dependencies
    // and is guaranteed to compile for devkitARM, unlike a full FBX/glTF library -- see
    // ROADMAP.md for that trade-off. MeshRendererComponent::Mesh (an AssetRef) resolves through
    // this the same way Material/Texture resolve through MaterialLoader/LoadTexture.
    class MeshLoader {
    public:
        // Returns a cached, empty-Vertices MeshData if `path` doesn't exist or fails to parse --
        // callers (see SceneRenderer.cpp's ResolveMeshGeometry) treat that as "fall back to the
        // entity's procedural MeshPrimitive", same graceful-degradation convention as every
        // other AssetRef resolution in this codebase. Cached by path, same convention as
        // MaterialLoader (no live-reload of a changed file while the Editor runs).
        static const MeshData& Load(const std::string& path);
    };

}
