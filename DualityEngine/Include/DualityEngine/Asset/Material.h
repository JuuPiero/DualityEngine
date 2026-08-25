#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // A reusable rendering asset (Unity's own Material concept), unlit-only for now (see
    // ROADMAP.md) -- MeshRendererComponent references one via an AssetRef instead of embedding
    // Color/Texture directly, so several meshes can share one look and editing the asset updates
    // every mesh that references it. Loaded from a ".mat" file via MaterialLoader.
    struct Material {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        AssetRef Texture; // empty Guid = flat Color, same convention as SpriteRendererComponent::Texture

        // Same MakeField()/FieldHandle reflection ScriptableObject and built-in components use
        // (Reflection/Field.h works on a plain void*, not just an Entity) -- lets MaterialLoader
        // and the Editor's Material asset inspector (DualityEditor/AssetInspectors.cpp) share the
        // exact same generic field (de)serialization and widget-drawing code instead of Material
        // hand-rolling its own, one-off versions of both.
        static std::vector<FieldHandle> Fields() {
            return {
                MakeColorField("Color", &Material::Color),
                MakeField("Texture", &Material::Texture),
            };
        }
    };

}
