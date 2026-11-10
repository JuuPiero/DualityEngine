#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

namespace Duality {

    class ThumbnailCache;

    // Editor helpers shared by every place that shows sprites (Project window sub-assets,
    // object fields, Animation window keyframes, Tile Palette): naming, thumbnails of a
    // sub-rect, native size.
    namespace SpriteAssetUI {
        struct SpriteEntry {
            std::string Reference; // "<textureGuid>#<spriteId>"
            std::string Name;
            glm::vec4 UV{ 0.0f, 0.0f, 1.0f, 1.0f };
            glm::vec2 SizePixels{ 0.0f };
        };

        // Sliced sprites of a Multiple-mode texture (empty otherwise / when not sliced).
        std::vector<SpriteEntry> ListSprites(const std::string& texturePath, const std::string& textureGuid);

        // "sprite name" for "guid#id", file name for a plain texture guid, "" when unresolved.
        std::string DisplayName(const std::string& reference);

        // Draws `reference` (whole texture or sub-sprite) fitted into [min, max] keeping its
        // aspect ratio. Returns false when the image could not be loaded.
        bool DrawThumbnail(ImDrawList* drawList, ThumbnailCache& thumbnails, const std::string& reference,
            const ImVec2& min, const ImVec2& max, ImU32 tint = IM_COL32_WHITE);

        // World size a sprite should get when dropped into the scene (Unity's native size):
        // rect pixels / Pixels Per Unit (texture importer PPU, else the project's). Returns
        // false when the size is unknown (e.g. a whole texture whose .meta has no size).
        bool NativeWorldSize(const std::string& reference, glm::vec2& outSize);

        // True when `reference` names an image (whole texture or sub-sprite).
        bool IsSpriteReference(const std::string& reference);

        // "<guid>" (index < 0) or "<guid>#<id of sprite index>" for a texture file (absolute or
        // relative to the project's Assets); "" when the file / sprite does not exist.
        std::string ReferenceForAsset(const std::string& assetPath, int spriteIndex);
    }

}
