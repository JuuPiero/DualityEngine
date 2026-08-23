#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // A reusable rendering asset (Unity's own Material concept), unlit-only for now (see
    // ROADMAP.md) -- MeshRendererComponent references one via an AssetRef instead of embedding
    // Color/Texture directly, so several meshes can share one look and editing the asset updates
    // every mesh that references it. Loaded from a ".material.json" file via MaterialLoader.
    struct Material {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        AssetRef Texture; // empty Guid = flat Color, same convention as SpriteRendererComponent::Texture
    };

}
