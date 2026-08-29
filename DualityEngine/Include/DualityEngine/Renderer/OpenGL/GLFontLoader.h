#pragma once

#include <string>
#include <vector>

#include <stb_truetype.h>

namespace Duality {

    // Reference pixel height a font is baked at -- large enough that headline-sized UI text
    // (rendered via DrawText's fontSize-relative quad scaling) won't visibly blur from
    // upscaling, small enough one atlas stays cheap. See GLFontLoader.cpp's own comment.
    constexpr float GLFontBakePixelHeight = 64.0f;
    constexpr int GLFontFirstChar = 32;   // ' '
    constexpr int GLFontNumChars = 95;    // ASCII 32-126 inclusive -- printable range only, no Unicode this pass

    struct GLFont {
        unsigned int AtlasTexture = 0; // 0 = failed to load
        int AtlasWidth = 0;
        int AtlasHeight = 0;
        std::vector<stbtt_bakedchar> Chars; // GLFontNumChars entries, index = codepoint - GLFontFirstChar
        // Distance from the baseline up to the top of the tallest glyph, in the SAME bake-scale
        // pixel units as Chars' own coordinates (both derived from GLFontBakePixelHeight) --
        // lets DrawText place a baseline cursor from a top-left position without re-opening the
        // font file at draw time.
        float Ascent = 0.0f;
    };

    // Shared stb_truetype -> OpenGL glyph-atlas loader, desktop-only (matches GLTextureLoader's
    // own conditional compile and single-purpose-loader-file convention). Used only by
    // OpenGLRenderer2D -- unlike GLTextureLoader there is no second caller (ThumbnailCache has
    // no reason to preview a font file), so this returns the full atlas+metrics bundle rather
    // than just a texture id: OpenGLRenderer2D needs the per-glyph metrics (stbtt_bakedchar) to
    // shape text itself, not just a texture to bind.
    class GLFontLoader {
    public:
        // Returns a GLFont with AtlasTexture == 0 if `path` doesn't exist or isn't a decodable
        // TrueType/OpenType file, or if no character fit even in the largest bitmap this tries.
        static GLFont LoadFontFromFile(const std::string& path);
    };

}
