#pragma once

#include <vector>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>

#include "DualityEngine/Renderer/MeshPrimitive.h"

namespace Duality {

    // Shared vertex layout for unlit and vertex-lit materials. Color is an optional
    // baked vertex-light/AO multiplier: primitives and OBJ files without colour
    // default to white, preserving existing material output byte-for-byte.
    struct MeshVertex {
        glm::vec3 Position;
        glm::vec2 TexCoord;
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        glm::vec4 Color{ 1.0f };
        // Unit tangent (+U direction) as signed bytes / 127, w = UV handedness (+-127). Never
        // stored in .obj/.dmesh files: GenerateTangents() derives it at load time, so the cooked
        // mesh format is unchanged. 4 bytes keeps the vertex at 72 bytes on PICA200 (GPU_BYTE
        // attribute, rescaled in the vertex shader) for Lit normal mapping.
        glm::i8vec4 Tangent{ 127, 0, 0, 127 };
        // Four influences are the cross-platform ceiling. Static/procedural meshes retain the
        // zero-weight default and therefore cost nothing in the ordinary MeshRenderer path.
        // Cooked skinned partitions contain at most 24 bones, so four byte-sized local
        // indices are sufficient. This removes 12 bytes from every skinned vertex compared
        // with the old u32 layout and maps directly to PICA's GPU_UNSIGNED_BYTE attribute.
        glm::u8vec4 BoneIndices{ 0 };
        glm::vec4 BoneWeights{ 0.0f };
    };

    // Flat, non-indexed triangle lists (GPU_TRIANGLES-compatible on both backends, matching
    // every citro3d reference example's own convention) for a unit-sized primitive centered
    // on the origin -- TransformComponent::Scale is what actually sizes it at draw time (see
    // MeshRendererComponent's own comment for why), so these are generated once and never
    // regenerated per-entity. Each backend uploads this vertex data to its own GPU-resident
    // buffer at Init() time; this function is the one place primitive geometry is defined,
    // shared by both platforms instead of duplicated per backend.
    const std::vector<MeshVertex>& GetPrimitiveMesh(MeshPrimitive primitive);

    // Fills MeshVertex::Tangent for a flat triangle list from its positions/UVs/normals
    // (smoothed across faces sharing a position + normal). Called for every primitive and
    // every mesh MeshLoader loads.
    void GenerateTangents(std::vector<MeshVertex>& vertices);

}
