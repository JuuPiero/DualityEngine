#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

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

    // Unity's Texture Importer "Sprite Mode": Single = the whole texture is one sprite (the
    // default, and what every texture authored before sprites existed is); Multiple = the
    // texture is a sheet sliced into named sub-sprites by the Sprite Editor.
    enum class SpriteImportMode { Single, Multiple };

    // One sprite of a Multiple-mode texture (Unity's SpriteMetaData). Stored in the texture's
    // ".meta" Importer block; referenced from AssetRef fields as "<textureGuid>#<Id>" (see
    // Asset/SpriteDatabase.h). Id is generated once and never changes, so renaming / moving /
    // resizing a sprite in the Sprite Editor keeps every reference to it valid.
    struct SpriteDefinition {
        std::string Id;
        std::string Name;
        // x, y, width, height in source-image pixels; (0,0) = the image's TOP-LEFT corner (the
        // engine's 2D convention is Y-down, unlike Unity's bottom-left sprite rects).
        glm::vec4 Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
        // Normalized within Rect, (0,0) = top-left -- same convention as SpriteRenderer::Pivot.
        glm::vec2 Pivot{ 0.5f, 0.5f };
        // 9-slice border in source pixels: left, right, top, bottom (SpriteRenderer::SliceBorder order).
        glm::vec4 Border{ 0.0f, 0.0f, 0.0f, 0.0f };

        bool operator==(const SpriteDefinition& other) const {
            return Id == other.Id && Name == other.Name && Rect == other.Rect && Pivot == other.Pivot && Border == other.Border;
        }
        bool operator!=(const SpriteDefinition& other) const { return !(*this == other); }
    };

    struct TextureImportSettings {
        TextureFilterMode FilterMode = TextureFilterMode::Bilinear;
        TextureWrapMode WrapMode = TextureWrapMode::Clamp;
        bool GenerateMipmaps = false;

        // ---- 3DS cooking (optional keys; absent = the previous fixed behaviour) -------------
        // tex3ds output format: Auto (tex3ds default, RGBA8) | RGBA8 | RGB8 | RGBA5551 |
        // RGB565 | RGBA4 | ETC1 | ETC1A4. Model importer textures inherit the model's setting.
        std::string N3DSFormat = "Auto";
        // Largest side the 3DS cook keeps (0 = the hardware maximum, 1024).
        int MaxSize = 0;

        // ---- Sprite import (all optional: absent keys = the pre-sprite behaviour) ----------
        SpriteImportMode SpriteMode = SpriteImportMode::Single;
        // Source pixels per world unit used when a sprite is dropped into the scene (native
        // size). 0 = use the project's Pixels Per Unit (Physics2D settings).
        float PixelsPerUnit = 0.0f;
        // Source image size the sprite rects were authored against (written by the Sprite
        // Editor). Rects are converted to normalized UVs with it, so a build that resizes the
        // texture (3DS 1024px limit) or pads it to a power of two keeps every sprite correct.
        int Width = 0;
        int Height = 0;
        // Single mode: the whole-texture sprite's pivot / border from the Sprite Editor.
        // HasSpritePivot = the pivot was authored explicitly and overrides SpriteRenderer::Pivot.
        bool HasSpritePivot = false;
        glm::vec2 SpritePivot{ 0.5f, 0.5f };
        glm::vec4 SpriteBorder{ 0.0f, 0.0f, 0.0f, 0.0f };
        // Multiple mode: the sliced sprites, in Sprite Editor order (tilemaps index this list).
        std::vector<SpriteDefinition> Sprites;

        const SpriteDefinition* FindSprite(const std::string& id) const;
        int FindSpriteIndex(const std::string& id) const;
        // Sprite part only (mode, PPU, size, single pivot/border, sprite list) -- what the
        // Sprite Editor's Apply/Revert compare.
        bool SpriteDataEquals(const TextureImportSettings& other) const;

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
