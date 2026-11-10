#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Scene/SceneRuntimeSystems.h" // TilemapData

namespace Duality {

    // ".tilemap" asset access shared by the renderers (cached -- the file used to be re-read
    // every frame) and the editor's Tile Palette (painting writes through Store/Save).
    // File format (unchanged): { "Width": w, "Height": h, "Tiles": [ index or -1, ... ] },
    // row-major, row 0 = the tilemap's top row. A tile index is the index of a sprite in the
    // tileset texture's sprite list (Sprite Editor, Multiple mode); a tileset without sliced
    // sprites draws its whole texture for every non-empty tile (the original behaviour).
    class TilemapAsset {
    public:
        // Cached load (an empty TilemapData when the file is missing / unreadable).
        static const TilemapData& Get(const std::string& path);
        // Replaces the cached data (the editor paints into memory, then saves).
        static void Store(const std::string& path, const TilemapData& data);
        static bool Save(const std::string& path, const TilemapData& data);
        static void Invalidate(const std::string& path = std::string());
    };

    // Pure tile-grid operations (Tile Palette brushes), unit-tested in Tests/.
    namespace TilemapEdit {
        // Grows the backing array to Width*Height (missing tiles = -1).
        void Normalize(TilemapData& data);
        bool InBounds(const TilemapData& data, int x, int y);
        int Get(const TilemapData& data, int x, int y); // -1 = empty / outside
        // Returns true when the tile changed. Outside the grid = no-op.
        bool Set(TilemapData& data, int x, int y, int tile);
        // Resizes keeping existing tiles at the same (x, y).
        void Resize(TilemapData& data, int width, int height);
        // Fills the inclusive rectangle (corners in any order); returns tiles changed.
        int BoxFill(TilemapData& data, glm::ivec2 a, glm::ivec2 b, int tile);
        // 4-connected flood fill of the region sharing (x, y)'s value; returns tiles changed.
        int FloodFill(TilemapData& data, int x, int y, int tile);
        // Cells on the line from a to b (inclusive), so a fast drag paints without gaps.
        std::vector<glm::ivec2> Line(glm::ivec2 a, glm::ivec2 b);
        // Brush stamp: `brush` is brushSize.x * brushSize.y tiles (row-major, -1 = skip),
        // its top-left placed at `origin`. Returns tiles changed.
        int Stamp(TilemapData& data, glm::ivec2 origin, glm::ivec2 brushSize, const std::vector<int>& brush);
    }

}
