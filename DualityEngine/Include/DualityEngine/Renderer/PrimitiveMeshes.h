#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/MeshPrimitive.h"

namespace Duality {

    // Shared vertex layout for unlit and vertex-lit materials. Normal is object-space.
    struct MeshVertex {
        glm::vec3 Position;
        glm::vec2 TexCoord;
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
    };

    // Flat, non-indexed triangle lists (GPU_TRIANGLES-compatible on both backends, matching
    // every citro3d reference example's own convention) for a unit-sized primitive centered
    // on the origin -- TransformComponent::Scale is what actually sizes it at draw time (see
    // MeshRendererComponent's own comment for why), so these are generated once and never
    // regenerated per-entity. Each backend uploads this vertex data to its own GPU-resident
    // buffer at Init() time; this function is the one place primitive geometry is defined,
    // shared by both platforms instead of duplicated per backend.
    const std::vector<MeshVertex>& GetPrimitiveMesh(MeshPrimitive primitive);

}
