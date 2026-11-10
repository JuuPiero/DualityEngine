#include "DualityEditor/Windows/SpriteAssetUI.h"

#include <algorithm>
#include <filesystem>

#include <stb_image.h>

#include "DualityEditor/ThumbnailCache.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Project/Project.h"

namespace Duality {

    namespace SpriteAssetUI {

        std::vector<SpriteEntry> ListSprites(const std::string& texturePath, const std::string& textureGuid) {
            std::vector<SpriteEntry> entries;
            if (texturePath.empty() || textureGuid.empty())
                return entries;
            const TextureImportSettings& settings = SpriteDatabase::Settings(texturePath);
            if (settings.SpriteMode != SpriteImportMode::Multiple)
                return entries;
            for (int i = 0; i < static_cast<int>(settings.Sprites.size()); ++i) {
                ResolvedSprite resolved;
                SpriteDatabase::Describe(texturePath, settings, i, resolved);
                SpriteEntry entry;
                entry.Reference = SpriteRef::Make(textureGuid, settings.Sprites[static_cast<size_t>(i)].Id);
                entry.Name = settings.Sprites[static_cast<size_t>(i)].Name;
                entry.UV = resolved.UV;
                entry.SizePixels = { settings.Sprites[static_cast<size_t>(i)].Rect.z, settings.Sprites[static_cast<size_t>(i)].Rect.w };
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        std::string DisplayName(const std::string& reference) {
            ResolvedSprite resolved;
            if (!SpriteDatabase::Resolve(reference, resolved))
                return {};
            if (resolved.IsSubSprite())
                return resolved.Name;
            return std::filesystem::path(resolved.TexturePath).filename().string();
        }

        bool DrawThumbnail(ImDrawList* drawList, ThumbnailCache& thumbnails, const std::string& reference,
            const ImVec2& min, const ImVec2& max, ImU32 tint) {
            ResolvedSprite resolved;
            if (!SpriteDatabase::Resolve(reference, resolved))
                return false;
            const uint32_t texture = thumbnails.GetThumbnail(resolved.TexturePath);
            if (!texture)
                return false;
            glm::vec2 size = resolved.SizePixels;
            if (size.x <= 0.0f || size.y <= 0.0f)
                size = { 1.0f, 1.0f };
            const float boxW = max.x - min.x, boxH = max.y - min.y;
            const float scale = std::min(boxW / size.x, boxH / size.y);
            const ImVec2 drawSize(size.x * scale, size.y * scale);
            const ImVec2 a(min.x + (boxW - drawSize.x) * 0.5f, min.y + (boxH - drawSize.y) * 0.5f);
            drawList->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture)), a, ImVec2(a.x + drawSize.x, a.y + drawSize.y),
                ImVec2(resolved.UV.x, resolved.UV.y), ImVec2(resolved.UV.z, resolved.UV.w), tint);
            return true;
        }

        bool NativeWorldSize(const std::string& reference, glm::vec2& outSize) {
            ResolvedSprite resolved;
            if (!SpriteDatabase::Resolve(reference, resolved))
                return false;
            glm::vec2 pixels = resolved.SizePixels;
            if (pixels.x <= 0.0f || pixels.y <= 0.0f) {
                int width = 0, height = 0, channels = 0;
                if (!stbi_info(resolved.TexturePath.c_str(), &width, &height, &channels) || width <= 0 || height <= 0)
                    return false;
                pixels = { static_cast<float>(width), static_cast<float>(height) };
            }
            const float ppu = resolved.PixelsPerUnit > 0.0f ? resolved.PixelsPerUnit : PhysicsUnits::PPU();
            if (ppu <= 0.0f)
                return false;
            outSize = pixels / ppu;
            return true;
        }

        bool IsSpriteReference(const std::string& reference) {
            const std::string path = AssetDatabase::ResolvePath(reference);
            return !path.empty() && ThumbnailCache::IsImageFile(std::filesystem::path(path).extension().string());
        }

        std::string ReferenceForAsset(const std::string& assetPath, int spriteIndex) {
            std::string path = assetPath;
            if (!std::filesystem::exists(path))
                if (auto project = Project::GetActive())
                    path = project->GetAssetsDirectory() + "/" + assetPath;
            if (!std::filesystem::exists(path))
                return {};
            const std::string guid = AssetMeta::EnsureMetaFile(path);
            AssetDatabase::Register(guid, path);
            if (spriteIndex < 0)
                return guid;
            SpriteDatabase::Invalidate(path);
            const TextureImportSettings& settings = SpriteDatabase::Settings(path);
            if (spriteIndex >= static_cast<int>(settings.Sprites.size()))
                return {};
            return SpriteRef::Make(guid, settings.Sprites[static_cast<size_t>(spriteIndex)].Id);
        }
    }

}
