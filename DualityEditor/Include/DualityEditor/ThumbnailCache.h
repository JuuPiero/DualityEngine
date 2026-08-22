#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Duality {

    // Loads and caches OpenGL textures for image-asset thumbnails shown in
    // the Content Browser. One instance's worth of textures live for as
    // long as the cache itself does (no eviction yet -- fine for a small
    // sample project's worth of assets).
    class ThumbnailCache {
    public:
        ~ThumbnailCache();

        // Returns an OpenGL texture id for the image at `path` (loading and
        // caching it on first request), or 0 if it isn't a recognized image
        // or failed to load.
        uint32_t GetThumbnail(const std::string& path);

        static bool IsImageFile(const std::string& extension);

    private:
        std::unordered_map<std::string, uint32_t> m_Cache;
    };

}
