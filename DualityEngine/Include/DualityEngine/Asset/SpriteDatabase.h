#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Asset/TextureImportSettings.h"

namespace Duality {

    // Sprite references inside an AssetRef string. A plain texture GUID keeps meaning "the
    // whole texture" (every scene authored before sprites existed); "<textureGuid>#<spriteId>"
    // names one sprite of a Multiple-mode texture (Unity's sub-asset of a texture).
    // AssetDatabase::ResolvePath ignores the "#..." part, so any code that only wants the
    // texture file keeps working with a sprite reference unchanged.
    namespace SpriteRef {
        constexpr char Separator = '#';
        std::string Make(const std::string& textureGuid, const std::string& spriteId);
        // The texture GUID part (the whole string when there is no '#').
        std::string TextureGuid(const std::string& reference);
        // The sprite id part ("" when the reference is a whole texture).
        std::string SpriteId(const std::string& reference);
        bool IsSubSprite(const std::string& reference);
    }

    // A sprite reference resolved to what a renderer needs.
    struct ResolvedSprite {
        std::string TexturePath;
        std::string Name;          // sprite name (texture file stem for a whole texture)
        int Index = -1;            // index in TextureImportSettings::Sprites, -1 = whole texture
        glm::vec4 UV{ 0.0f, 0.0f, 1.0f, 1.0f }; // normalized u0, v0, u1, v1 (v = 0 at the image top)
        glm::vec2 SizePixels{ 0.0f, 0.0f };     // sprite rect size in source pixels (0 = unknown)
        glm::vec2 TextureSizePixels{ 0.0f, 0.0f };
        glm::vec4 BorderPixels{ 0.0f, 0.0f, 0.0f, 0.0f }; // left, right, top, bottom
        bool HasPivot = false;     // true = Pivot comes from the sprite and overrides the renderer's
        glm::vec2 Pivot{ 0.5f, 0.5f };
        float PixelsPerUnit = 0.0f; // texture importer PPU (0 = project PPU)

        bool IsSubSprite() const { return Index >= 0; }
        // Normalized border (fraction of the texture) for a 9-slice draw; derived from
        // BorderPixels and TextureSizePixels (zero when the texture size is unknown).
        glm::vec4 BorderUV() const;
    };

    // Runtime lookup of sprite data, cached per texture path (the .meta is parsed once). Used
    // by every 2D renderer path on desktop and 3DS (where the cooked ".t3x.meta" carries the
    // same Importer block, see BuildPipeline::CookAssets).
    class SpriteDatabase {
    public:
        // Import settings of a texture file, cached. Never fails (defaults when no .meta).
        static const TextureImportSettings& Settings(const std::string& texturePath);

        // Resolves "guid" or "guid#spriteId". False when the reference is empty or its texture
        // is unknown. An unknown sprite id falls back to the whole texture (still true).
        static bool Resolve(const std::string& reference, ResolvedSprite& out);
        // The index-th sprite of a Multiple-mode texture (tilemaps / sprite-sheet animators).
        // False when the texture has no such sprite.
        static bool ResolveIndex(const std::string& textureGuid, int index, ResolvedSprite& out);
        // Number of sliced sprites of a texture (0 for Single mode / no data).
        static int SpriteCount(const std::string& textureGuid);

        // Pure helper behind Resolve: fills `out` for sprite `index` (-1 = whole texture).
        static void Describe(const std::string& texturePath, const TextureImportSettings& settings, int index, ResolvedSprite& out);

        // Drops cached settings (one texture, or all when `texturePath` is empty). The editor
        // calls this after the Sprite Editor / texture Inspector rewrote a .meta.
        static void Invalidate(const std::string& texturePath = std::string());
        // Bumped by every Invalidate -- editor views cache derived data against it.
        static uint32_t Version();
    };

    // ---- Slicing (Sprite Editor > Slice) ------------------------------------------------------
    // Pure functions over an RGBA8 image (row 0 = top), so they are unit-testable and reusable
    // by tools. Rects are x, y, width, height in pixels, top-left origin.
    struct SpriteSliceRect { int X = 0, Y = 0, Width = 0, Height = 0; };

    namespace SpriteSlicer {
        // True when any pixel of the rect has alpha > alphaThreshold (null image = never empty).
        bool HasOpaquePixels(const uint8_t* rgba, int width, int height, const SpriteSliceRect& rect, uint8_t alphaThreshold = 0);

        // Unity "Grid By Cell Size": cells of cellSize, starting at offset, `padding` pixels
        // between cells. Empty (fully transparent) cells are dropped unless keepEmpty.
        std::vector<SpriteSliceRect> GridByCellSize(const uint8_t* rgba, int width, int height,
            glm::ivec2 cellSize, glm::ivec2 offset = glm::ivec2(0), glm::ivec2 padding = glm::ivec2(0), bool keepEmpty = false);

        // Unity "Grid By Cell Count": columns x rows cells filling the image after offset/padding.
        std::vector<SpriteSliceRect> GridByCellCount(const uint8_t* rgba, int width, int height,
            int columns, int rows, glm::ivec2 offset = glm::ivec2(0), glm::ivec2 padding = glm::ivec2(0), bool keepEmpty = false);

        // Unity "Automatic": bounding boxes of 8-connected islands of pixels with alpha >
        // alphaThreshold; overlapping boxes are merged, boxes smaller than minSize (both axes)
        // dropped. Sorted top-to-bottom, then left-to-right (rows grouped by vertical overlap).
        std::vector<SpriteSliceRect> Automatic(const uint8_t* rgba, int width, int height,
            int minSize = 1, uint8_t alphaThreshold = 0);

        // Turns rects into sprite definitions named "<baseName>_<i>", reusing the Id of an
        // existing sprite with the same name (Unity keeps references alive across a re-slice).
        std::vector<SpriteDefinition> MakeSprites(const std::vector<SpriteSliceRect>& rects, const std::string& baseName,
            const glm::vec2& pivot, const std::vector<SpriteDefinition>& existing);

        // A new sprite id that is not used by `existing` (16 hex chars).
        std::string NewSpriteId(const std::vector<SpriteDefinition>& existing);
    }

}
