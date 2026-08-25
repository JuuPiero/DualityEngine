#pragma once

#include <string>

#include "DualityEngine/Asset/TextureImportSettings.h"

namespace Duality {

    // Shared stb_image -> OpenGL texture loader, desktop-only (matches
    // OpenGLRenderer2D's own conditional compile). Used by both
    // ThumbnailCache (Content Browser previews) and OpenGLRenderer2D (real
    // sprite textures) so the stb_image/glTexImage2D loading code exists
    // in exactly one place.
    class GLTextureLoader {
    public:
        // Returns a new OpenGL texture id, or 0 if `path` doesn't exist or
        // isn't a decodable image. Caller owns the returned texture (no
        // caching here -- each caller keeps its own path->id cache, since
        // ThumbnailCache and OpenGLRenderer2D have different eviction
        // needs). `settings` defaults to TextureImportSettings{}'s own
        // defaults (Bilinear/Clamp/no mipmaps, matching this loader's
        // original hardcoded behavior) -- ThumbnailCache intentionally
        // doesn't look up a texture's real import settings for its small
        // preview, only OpenGLRenderer2D's real sprite-texture path does.
        static unsigned int LoadTextureFromFile(const std::string& path, const TextureImportSettings& settings = {});
    };

}
