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

        // Object-space bounds of Vertices (all formats; zero for an empty mesh).
        glm::vec3 BoundsMin{ 0.0f };
        glm::vec3 BoundsMax{ 0.0f };
        // .dmesh version the data came from (0 = .obj). Diagnostics / tests only.
        uint32_t SourceVersion = 0;
        // Model importer's Read/Write flag (V4 header). Stored for tools; both players keep
        // the CPU copy today because culling/skinning read it.
        bool ReadWriteEnabled = true;
    };

    // .dmesh format (little endian, explicit scalar fields -- never raw structs):
    //   V1: "DMSH" u32 version u32 vertexCount u32 subMeshCount f32 radius,
    //       vertexCount x {f32 pos3 uv2 nrm3 col4}, subMeshCount x {u32 first u32 count}
    //   V2: + u32 boneCount after radius, bones {u32 len, name, f32 offset[16]}, vertices +
    //       u32 boneIds[4] f32 weights[4]
    //   V3: vertex bone ids are u8 palette-local ids; each submesh ends with
    //       {u32 paletteCount, u16 palette[]} (<= DMeshMaxPaletteBones)
    //   V4 (ModelImporter, current): indexed + optionally quantized. Header
    //       u32 flags (DMeshFlag_*) u32 vertexCount u32 indexCount u32 subMeshCount f32 radius
    //       f32 boundsMin[3] boundsMax[3] uvMin[2] uvMax[2] u32 boneCount, bones as V2,
    //       vertexCount vertices (V3 layout, or quantized: u16 pos[3] in bounds, u16 uv[2] in the
    //       uv range, s8 normal[3] + pad, u8 color[4], u8 boneIds[4], u8 weights[4]),
    //       indexCount u16 indices (relative to their submesh's FirstVertex), then per submesh
    //       {u32 firstVertex u32 vertexCount u32 firstIndex u32 indexCount u32 materialSlot
    //        u32 paletteCount u16 palette[]}.
    //   The loader always expands to the flat, non-indexed MeshVertex list both renderers draw,
    //   so every runtime consumer sees the same data as for V1-V3 files.
    constexpr uint32_t DMeshCurrentVersion = 4;
    // PICA200 skinning budget (mesh_skin*.v.pica: 24 affine bones = 72 float uniforms).
    constexpr uint32_t DMeshMaxPaletteBones = 24;
    // Unique vertices per V4 submesh: indices are u16 (citro3d's C3D_UNSIGNED_SHORT).
    constexpr uint32_t DMeshMaxVerticesPerSubMesh = 65535;
    // Influences per vertex the skinned PICA/GL shaders read.
    constexpr uint32_t DMeshMaxBonesPerVertex = 4;
    enum DMeshFlags : uint32_t {
        DMeshFlag_Quantized = 1u << 0,
        DMeshFlag_ReadWrite = 1u << 1,
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
        // Editor re-import: drops the cached copy of `path` (empty = every path). References
        // returned by an earlier Load of that path become invalid.
        static void Invalidate(const std::string& path = std::string());
        // Parses an in-memory .dmesh (any version). False + empty `out` on malformed data.
        static bool ParseDMesh(const uint8_t* data, size_t size, MeshData& out, std::string* error = nullptr);
    };

}
