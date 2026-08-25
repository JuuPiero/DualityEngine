#pragma once

#include <string>

namespace Duality {

    // Unity's TextureImporter-equivalent: how a texture asset should be sampled, stored in its
    // ".meta" sidecar (an "Importer" JSON block, alongside the existing "guid" key) rather than
    // in the asset file itself, since a .png/.jpg is a foreign binary format this engine doesn't
    // own -- unlike Material/Prefab/ScriptableObject, there's nowhere else to put engine-specific
    // settings for it. Mirrors what was already hardcoded: GLTextureLoader defaulted to
    // GL_LINEAR + GL_CLAMP_TO_EDGE with no mipmaps, so those are this struct's own defaults too
    // -- registering these settings changes nothing for a texture that never gets its .meta
    // edited.
    enum class TextureFilterMode { Point, Bilinear };
    enum class TextureWrapMode { Clamp, Repeat };

    struct TextureImportSettings {
        TextureFilterMode FilterMode = TextureFilterMode::Bilinear;
        TextureWrapMode WrapMode = TextureWrapMode::Clamp;
        bool GenerateMipmaps = false;

        // Reads `assetPath`'s ".meta" "Importer" block, defaulting whatever's missing/absent --
        // same graceful-degradation convention as MaterialLoader::Load. `assetPath` is the
        // texture file itself (e.g. "Textures/player.png"), not the ".meta" path.
        static TextureImportSettings Load(const std::string& assetPath);

        // Merges into `assetPath`'s ".meta" file, preserving its "guid" (and any other unrelated
        // top-level keys) -- the file must already exist (AssetMeta::EnsureMetaFile creates it
        // with just a guid the first time the Content Browser sees the asset).
        static bool Save(const std::string& assetPath, const TextureImportSettings& settings);
    };

}
