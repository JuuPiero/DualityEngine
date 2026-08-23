#include "DualityEditor/ThumbnailCache.h"

#include <algorithm>
#include <cctype>

#include <GL/glew.h>

#include "DualityEngine/Renderer/OpenGL/GLTextureLoader.h"

namespace Duality {

    bool ThumbnailCache::IsImageFile(const std::string& extension) {
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
    }

    uint32_t ThumbnailCache::GetThumbnail(const std::string& path) {
        auto it = m_Cache.find(path);
        if (it != m_Cache.end())
            return it->second;

        uint32_t texture = GLTextureLoader::LoadTextureFromFile(path);
        m_Cache[path] = texture;
        return texture;
    }

    ThumbnailCache::~ThumbnailCache() {
        for (auto& [path, texture] : m_Cache) {
            if (texture) {
                GLuint id = texture;
                glDeleteTextures(1, &id);
            }
        }
    }

}
