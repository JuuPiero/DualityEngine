#pragma once

#include <string>

#include "DualityEngine/Asset/Material.h"

namespace Duality {

    // Loads/saves ".material.json" files -- same std::ifstream/nlohmann::json convention as
    // AssetMeta.cpp and SceneSerializer.cpp, works identically on desktop and 3DS (a Material
    // asset is plain data copied into romfs as-is by BuildPipeline::CookAssets, same as
    // Scene.json -- no special cooking step needed).
    class MaterialLoader {
    public:
        // Returns a default Material (white, no texture) if `path` doesn't exist or fails to
        // parse -- same graceful-degradation convention as an empty/unresolved AssetRef
        // elsewhere in this codebase. Cached by path (a Material asset is expected to be small
        // and load-once; there is no live-reload of a changed file while the Editor runs, same
        // convention as a renderer's own LoadTexture cache).
        static Material Load(const std::string& path);

        // Writes `material` to `path` as JSON, overwriting it if it already exists -- used by
        // the Editor's "Create Material" flow.
        static bool Save(const std::string& path, const Material& material);
    };

}
