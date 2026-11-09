#pragma once

#include <string>

#include "DualityEngine/Asset/Material.h"

namespace Duality {

    // Loads/saves ".mat" files -- same std::ifstream/nlohmann::json convention as
    // AssetMeta.cpp and SceneSerializer.cpp, works identically on desktop and 3DS (a Material
    // asset is plain data copied into romfs as-is by BuildPipeline::CookAssets, same as
    // Scene.scene -- no special cooking step needed).
    //
    // File format (version 2, Unity-like property names):
    //   { "Version": 2, "Shader": "Lit/Specular", "Surface": "Opaque", "Cull": "Back",
    //     "ZWrite": "Auto", "Render Queue": -1,
    //     "Properties": { "_Color": [r,g,b,a], "_MainTex": "<guid>", "_MainTex_ST": [sx,sy,ox,oy],
    //                     "_BumpMap": "<guid>", "_Cutoff": 0.5, "_SpecColor": [...], "_Shininess": 0.4,
    //                     "_EmissionColor": [...], "_ShadeColor": [...], "_ToonSteps": 1,
    //                     "_ToonSmoothness": 0.05 } }
    // Version 1 files ({ "Color", "Texture", "Shading Mode" }) still load and are migrated in
    // memory to Unlit/Texture or Mobile/VertexLit with an identical look (see
    // MaterialLoader.cpp's FromLegacyJson). They are only rewritten when edited.
    class MaterialLoader {
    public:
        // Returns a default Material (legacy Unlit white, no texture) if `path` doesn't exist
        // or fails to parse -- same graceful-degradation convention as an empty/unresolved
        // AssetRef elsewhere in this codebase. Cached by path.
        static Material Load(const std::string& path);

        // Writes `material` to `path` as JSON, overwriting it if it already exists, and refreshes
        // the cache so every renderer (Scene view, Game view) sees the edit on the next frame.
        static bool Save(const std::string& path, const Material& material);

        // In-memory (de)serialization, shared by Load/Save -- exposed for tests and tools.
        static Material ParseJsonText(const std::string& text);
        static std::string ToJsonText(const Material& material);

        // Drops every cached material (scene transitions / tests that rewrite files).
        static void ClearCache();
    };

}
