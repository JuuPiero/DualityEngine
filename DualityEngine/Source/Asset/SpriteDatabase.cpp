#include "DualityEngine/Asset/SpriteDatabase.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "DualityEngine/Asset/AssetDatabase.h"

namespace Duality {

    // ---- SpriteRef -----------------------------------------------------------------------------

    std::string SpriteRef::Make(const std::string& textureGuid, const std::string& spriteId) {
        if (spriteId.empty())
            return textureGuid;
        return textureGuid + Separator + spriteId;
    }

    std::string SpriteRef::TextureGuid(const std::string& reference) {
        const size_t separator = reference.find(Separator);
        return separator == std::string::npos ? reference : reference.substr(0, separator);
    }

    std::string SpriteRef::SpriteId(const std::string& reference) {
        const size_t separator = reference.find(Separator);
        return separator == std::string::npos ? std::string() : reference.substr(separator + 1);
    }

    bool SpriteRef::IsSubSprite(const std::string& reference) {
        return reference.find(Separator) != std::string::npos;
    }

    // ---- ResolvedSprite --------------------------------------------------------------------------

    glm::vec4 ResolvedSprite::BorderUV() const {
        if (TextureSizePixels.x <= 0.0f || TextureSizePixels.y <= 0.0f)
            return glm::vec4(0.0f);
        return { BorderPixels.x / TextureSizePixels.x, BorderPixels.y / TextureSizePixels.x,
                 BorderPixels.z / TextureSizePixels.y, BorderPixels.w / TextureSizePixels.y };
    }

    // ---- SpriteDatabase --------------------------------------------------------------------------

    namespace {
        std::unordered_map<std::string, TextureImportSettings> s_Settings;
        uint32_t s_Version = 1;
    }

    const TextureImportSettings& SpriteDatabase::Settings(const std::string& texturePath) {
        auto found = s_Settings.find(texturePath);
        if (found != s_Settings.end())
            return found->second;
        return s_Settings.emplace(texturePath, TextureImportSettings::Load(texturePath)).first->second;
    }

    void SpriteDatabase::Describe(const std::string& texturePath, const TextureImportSettings& settings, int index, ResolvedSprite& out) {
        out = ResolvedSprite{};
        out.TexturePath = texturePath;
        out.PixelsPerUnit = settings.PixelsPerUnit;
        const bool knownSize = settings.Width > 0 && settings.Height > 0;
        if (knownSize)
            out.TextureSizePixels = { static_cast<float>(settings.Width), static_cast<float>(settings.Height) };

        if (index >= 0 && index < static_cast<int>(settings.Sprites.size()) && knownSize) {
            const SpriteDefinition& sprite = settings.Sprites[static_cast<size_t>(index)];
            out.Index = index;
            out.Name = sprite.Name;
            const float w = static_cast<float>(settings.Width);
            const float h = static_cast<float>(settings.Height);
            out.UV = { sprite.Rect.x / w, sprite.Rect.y / h, (sprite.Rect.x + sprite.Rect.z) / w, (sprite.Rect.y + sprite.Rect.w) / h };
            out.SizePixels = { sprite.Rect.z, sprite.Rect.w };
            out.BorderPixels = sprite.Border;
            out.HasPivot = true;
            out.Pivot = sprite.Pivot;
            return;
        }

        out.Name = std::filesystem::path(texturePath).stem().string();
        out.SizePixels = out.TextureSizePixels;
        out.BorderPixels = settings.SpriteBorder;
        out.HasPivot = settings.HasSpritePivot;
        out.Pivot = settings.SpritePivot;
    }

    bool SpriteDatabase::Resolve(const std::string& reference, ResolvedSprite& out) {
        if (reference.empty())
            return false;
        const std::string guid = SpriteRef::TextureGuid(reference);
        const std::string path = AssetDatabase::ResolvePath(guid);
        if (path.empty())
            return false;
        const TextureImportSettings& settings = Settings(path);
        const std::string id = SpriteRef::SpriteId(reference);
        Describe(path, settings, id.empty() ? -1 : settings.FindSpriteIndex(id), out);
        return true;
    }

    bool SpriteDatabase::ResolveIndex(const std::string& textureGuid, int index, ResolvedSprite& out) {
        const std::string path = AssetDatabase::ResolvePath(SpriteRef::TextureGuid(textureGuid));
        if (path.empty())
            return false;
        const TextureImportSettings& settings = Settings(path);
        if (index < 0 || index >= static_cast<int>(settings.Sprites.size()) || settings.Width <= 0 || settings.Height <= 0)
            return false;
        Describe(path, settings, index, out);
        return true;
    }

    int SpriteDatabase::SpriteCount(const std::string& textureGuid) {
        const std::string path = AssetDatabase::ResolvePath(SpriteRef::TextureGuid(textureGuid));
        if (path.empty())
            return 0;
        const TextureImportSettings& settings = Settings(path);
        return settings.SpriteMode == SpriteImportMode::Multiple ? static_cast<int>(settings.Sprites.size()) : 0;
    }

    void SpriteDatabase::Invalidate(const std::string& texturePath) {
        if (texturePath.empty())
            s_Settings.clear();
        else
            s_Settings.erase(texturePath);
        ++s_Version;
    }

    uint32_t SpriteDatabase::Version() {
        return s_Version;
    }

    // ---- SpriteSlicer ---------------------------------------------------------------------------

    namespace SpriteSlicer {

        bool HasOpaquePixels(const uint8_t* rgba, int width, int height, const SpriteSliceRect& rect, uint8_t alphaThreshold) {
            if (!rgba)
                return true;
            const int x0 = std::max(0, rect.X), y0 = std::max(0, rect.Y);
            const int x1 = std::min(width, rect.X + rect.Width), y1 = std::min(height, rect.Y + rect.Height);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    if (rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] > alphaThreshold)
                        return true;
            return false;
        }

        std::vector<SpriteSliceRect> GridByCellSize(const uint8_t* rgba, int width, int height,
            glm::ivec2 cellSize, glm::ivec2 offset, glm::ivec2 padding, bool keepEmpty) {
            std::vector<SpriteSliceRect> rects;
            if (cellSize.x <= 0 || cellSize.y <= 0 || width <= 0 || height <= 0)
                return rects;
            padding = glm::max(padding, glm::ivec2(0));
            offset = glm::max(offset, glm::ivec2(0));
            for (int y = offset.y; y + cellSize.y <= height; y += cellSize.y + padding.y) {
                for (int x = offset.x; x + cellSize.x <= width; x += cellSize.x + padding.x) {
                    const SpriteSliceRect rect{ x, y, cellSize.x, cellSize.y };
                    if (keepEmpty || HasOpaquePixels(rgba, width, height, rect))
                        rects.push_back(rect);
                }
            }
            return rects;
        }

        std::vector<SpriteSliceRect> GridByCellCount(const uint8_t* rgba, int width, int height,
            int columns, int rows, glm::ivec2 offset, glm::ivec2 padding, bool keepEmpty) {
            if (columns <= 0 || rows <= 0)
                return {};
            padding = glm::max(padding, glm::ivec2(0));
            offset = glm::max(offset, glm::ivec2(0));
            const int usableW = width - offset.x - padding.x * (columns - 1);
            const int usableH = height - offset.y - padding.y * (rows - 1);
            const glm::ivec2 cell{ usableW / columns, usableH / rows };
            if (cell.x <= 0 || cell.y <= 0)
                return {};
            std::vector<SpriteSliceRect> rects;
            for (int row = 0; row < rows; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const SpriteSliceRect rect{ offset.x + column * (cell.x + padding.x), offset.y + row * (cell.y + padding.y), cell.x, cell.y };
                    if (keepEmpty || HasOpaquePixels(rgba, width, height, rect))
                        rects.push_back(rect);
                }
            }
            return rects;
        }

        std::vector<SpriteSliceRect> Automatic(const uint8_t* rgba, int width, int height, int minSize, uint8_t alphaThreshold) {
            std::vector<SpriteSliceRect> boxes;
            if (!rgba || width <= 0 || height <= 0)
                return boxes;
            const size_t count = static_cast<size_t>(width) * height;
            std::vector<uint8_t> visited(count, 0);
            std::vector<int> stack;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t start = static_cast<size_t>(y) * width + x;
                    if (visited[start] || rgba[start * 4 + 3] <= alphaThreshold)
                        continue;
                    int minX = x, maxX = x, minY = y, maxY = y;
                    visited[start] = 1;
                    stack.clear();
                    stack.push_back(static_cast<int>(start));
                    while (!stack.empty()) {
                        const int index = stack.back();
                        stack.pop_back();
                        const int px = index % width, py = index / width;
                        minX = std::min(minX, px); maxX = std::max(maxX, px);
                        minY = std::min(minY, py); maxY = std::max(maxY, py);
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = px + dx, ny = py + dy;
                                if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height)
                                    continue;
                                const size_t neighbour = static_cast<size_t>(ny) * width + nx;
                                if (visited[neighbour] || rgba[neighbour * 4 + 3] <= alphaThreshold)
                                    continue;
                                visited[neighbour] = 1;
                                stack.push_back(static_cast<int>(neighbour));
                            }
                        }
                    }
                    boxes.push_back({ minX, minY, maxX - minX + 1, maxY - minY + 1 });
                }
            }

            // Merge overlapping boxes (e.g. a character whose detached eye sits inside its outline).
            bool merged = true;
            while (merged) {
                merged = false;
                for (size_t i = 0; i < boxes.size() && !merged; ++i) {
                    for (size_t j = i + 1; j < boxes.size(); ++j) {
                        const SpriteSliceRect& a = boxes[i];
                        const SpriteSliceRect& b = boxes[j];
                        const bool overlap = a.X < b.X + b.Width && b.X < a.X + a.Width && a.Y < b.Y + b.Height && b.Y < a.Y + a.Height;
                        if (!overlap)
                            continue;
                        const int x0 = std::min(a.X, b.X), y0 = std::min(a.Y, b.Y);
                        const int x1 = std::max(a.X + a.Width, b.X + b.Width), y1 = std::max(a.Y + a.Height, b.Y + b.Height);
                        boxes[i] = { x0, y0, x1 - x0, y1 - y0 };
                        boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(j));
                        merged = true;
                        break;
                    }
                }
            }

            minSize = std::max(1, minSize);
            boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [minSize](const SpriteSliceRect& r) {
                return r.Width < minSize && r.Height < minSize;
            }), boxes.end());

            // Reading order: group into rows by vertical overlap with the row's first box, then
            // left-to-right inside each row.
            std::sort(boxes.begin(), boxes.end(), [](const SpriteSliceRect& a, const SpriteSliceRect& b) {
                return a.Y != b.Y ? a.Y < b.Y : a.X < b.X;
            });
            std::vector<SpriteSliceRect> ordered;
            ordered.reserve(boxes.size());
            size_t rowStart = 0;
            while (rowStart < boxes.size()) {
                const int rowBottom = boxes[rowStart].Y + boxes[rowStart].Height;
                size_t rowEnd = rowStart + 1;
                while (rowEnd < boxes.size() && boxes[rowEnd].Y < rowBottom)
                    ++rowEnd;
                std::vector<SpriteSliceRect> row(boxes.begin() + static_cast<std::ptrdiff_t>(rowStart), boxes.begin() + static_cast<std::ptrdiff_t>(rowEnd));
                std::sort(row.begin(), row.end(), [](const SpriteSliceRect& a, const SpriteSliceRect& b) { return a.X < b.X; });
                ordered.insert(ordered.end(), row.begin(), row.end());
                rowStart = rowEnd;
            }
            return ordered;
        }

        std::string NewSpriteId(const std::vector<SpriteDefinition>& existing) {
            static std::mt19937_64 s_Engine{ std::random_device{}() };
            for (;;) {
                char buffer[17];
                std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(s_Engine()));
                const std::string id(buffer);
                const bool used = std::any_of(existing.begin(), existing.end(), [&](const SpriteDefinition& s) { return s.Id == id; });
                if (!used)
                    return id;
            }
        }

        std::vector<SpriteDefinition> MakeSprites(const std::vector<SpriteSliceRect>& rects, const std::string& baseName,
            const glm::vec2& pivot, const std::vector<SpriteDefinition>& existing) {
            std::unordered_map<std::string, const SpriteDefinition*> byName;
            for (const SpriteDefinition& sprite : existing)
                byName.emplace(sprite.Name, &sprite);
            std::vector<SpriteDefinition> result;
            result.reserve(rects.size());
            std::vector<SpriteDefinition> taken = existing; // ids that must stay unique
            std::unordered_set<std::string> reused;
            for (size_t i = 0; i < rects.size(); ++i) {
                SpriteDefinition sprite;
                sprite.Name = baseName + "_" + std::to_string(i);
                sprite.Rect = { static_cast<float>(rects[i].X), static_cast<float>(rects[i].Y), static_cast<float>(rects[i].Width), static_cast<float>(rects[i].Height) };
                sprite.Pivot = pivot;
                auto found = byName.find(sprite.Name);
                if (found != byName.end() && !reused.count(found->second->Id)) {
                    sprite.Id = found->second->Id;
                    sprite.Border = found->second->Border;
                    reused.insert(sprite.Id);
                } else {
                    sprite.Id = NewSpriteId(taken);
                    taken.push_back(sprite);
                }
                result.push_back(std::move(sprite));
            }
            return result;
        }
    }

}
