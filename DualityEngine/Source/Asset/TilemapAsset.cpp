#include "DualityEngine/Asset/TilemapAsset.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace Duality {

    namespace {
        std::unordered_map<std::string, TilemapData> s_Cache;
    }

    const TilemapData& TilemapAsset::Get(const std::string& path) {
        auto found = s_Cache.find(path);
        if (found != s_Cache.end())
            return found->second;
        return s_Cache.emplace(path, path.empty() ? TilemapData{} : TilemapLoader::Load(path)).first->second;
    }

    void TilemapAsset::Store(const std::string& path, const TilemapData& data) {
        s_Cache[path] = data;
    }

    bool TilemapAsset::Save(const std::string& path, const TilemapData& data) {
        nlohmann::json root = nlohmann::json::object();
        root["Width"] = data.Width;
        root["Height"] = data.Height;
        root["Tiles"] = data.Tiles;
        std::ofstream file(path);
        if (!file)
            return false;
        file << root.dump(2);
        s_Cache[path] = data;
        return static_cast<bool>(file);
    }

    void TilemapAsset::Invalidate(const std::string& path) {
        if (path.empty())
            s_Cache.clear();
        else
            s_Cache.erase(path);
    }

    namespace TilemapEdit {

        void Normalize(TilemapData& data) {
            data.Width = std::max(0, data.Width);
            data.Height = std::max(0, data.Height);
            data.Tiles.resize(static_cast<size_t>(data.Width) * data.Height, -1);
        }

        bool InBounds(const TilemapData& data, int x, int y) {
            return x >= 0 && y >= 0 && x < data.Width && y < data.Height;
        }

        int Get(const TilemapData& data, int x, int y) {
            if (!InBounds(data, x, y))
                return -1;
            const size_t index = static_cast<size_t>(y) * data.Width + x;
            return index < data.Tiles.size() ? data.Tiles[index] : -1;
        }

        bool Set(TilemapData& data, int x, int y, int tile) {
            if (!InBounds(data, x, y))
                return false;
            Normalize(data);
            tile = tile < 0 ? -1 : tile;
            int& cell = data.Tiles[static_cast<size_t>(y) * data.Width + x];
            if (cell == tile)
                return false;
            cell = tile;
            return true;
        }

        void Resize(TilemapData& data, int width, int height) {
            width = std::max(0, width);
            height = std::max(0, height);
            std::vector<int> tiles(static_cast<size_t>(width) * height, -1);
            for (int y = 0; y < std::min(height, data.Height); ++y)
                for (int x = 0; x < std::min(width, data.Width); ++x)
                    tiles[static_cast<size_t>(y) * width + x] = Get(data, x, y);
            data.Width = width;
            data.Height = height;
            data.Tiles = std::move(tiles);
        }

        int BoxFill(TilemapData& data, glm::ivec2 a, glm::ivec2 b, int tile) {
            const glm::ivec2 lo = glm::min(a, b), hi = glm::max(a, b);
            int changed = 0;
            for (int y = lo.y; y <= hi.y; ++y)
                for (int x = lo.x; x <= hi.x; ++x)
                    changed += Set(data, x, y, tile) ? 1 : 0;
            return changed;
        }

        int FloodFill(TilemapData& data, int x, int y, int tile) {
            if (!InBounds(data, x, y))
                return 0;
            Normalize(data);
            tile = tile < 0 ? -1 : tile;
            const int target = Get(data, x, y);
            if (target == tile)
                return 0;
            int changed = 0;
            std::vector<glm::ivec2> stack{ { x, y } };
            while (!stack.empty()) {
                const glm::ivec2 cell = stack.back();
                stack.pop_back();
                if (!InBounds(data, cell.x, cell.y) || Get(data, cell.x, cell.y) != target)
                    continue;
                data.Tiles[static_cast<size_t>(cell.y) * data.Width + cell.x] = tile;
                ++changed;
                stack.push_back({ cell.x + 1, cell.y });
                stack.push_back({ cell.x - 1, cell.y });
                stack.push_back({ cell.x, cell.y + 1 });
                stack.push_back({ cell.x, cell.y - 1 });
            }
            return changed;
        }

        std::vector<glm::ivec2> Line(glm::ivec2 a, glm::ivec2 b) {
            std::vector<glm::ivec2> cells;
            const int dx = std::abs(b.x - a.x), dy = -std::abs(b.y - a.y);
            const int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
            int error = dx + dy;
            for (;;) {
                cells.push_back(a);
                if (a == b)
                    break;
                const int e2 = 2 * error;
                if (e2 >= dy) { error += dy; a.x += sx; }
                if (e2 <= dx) { error += dx; a.y += sy; }
            }
            return cells;
        }

        int Stamp(TilemapData& data, glm::ivec2 origin, glm::ivec2 brushSize, const std::vector<int>& brush) {
            int changed = 0;
            for (int y = 0; y < brushSize.y; ++y) {
                for (int x = 0; x < brushSize.x; ++x) {
                    const size_t index = static_cast<size_t>(y) * brushSize.x + x;
                    if (index >= brush.size() || brush[index] < 0)
                        continue;
                    changed += Set(data, origin.x + x, origin.y + y, brush[index]) ? 1 : 0;
                }
            }
            return changed;
        }
    }

}
