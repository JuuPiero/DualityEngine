#include "DualityEngine/Renderer/OpenGL/GLFontLoader.h"

#include <fstream>
#include <vector>

#include <GL/glew.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        // stbtt_BakeFontBitmap returns <= 0 if the requested char range didn't fully fit in the
        // given bitmap size (0 = nothing fit, negative = -(count that fit)) -- try a small atlas
        // first (plenty for ASCII at 64px), retry once at 4x the area before giving up, matching
        // this codebase's own "try, retry bigger, then fail loudly" convention (e.g.
        // BuildPipeline's tex3ds retry logic).
        bool TryBake(const std::vector<unsigned char>& fontData, int size, std::vector<unsigned char>& outBitmap, std::vector<stbtt_bakedchar>& outChars) {
            outBitmap.assign(static_cast<size_t>(size) * size, 0);
            outChars.assign(GLFontNumChars, stbtt_bakedchar{});
            int result = stbtt_BakeFontBitmap(fontData.data(), 0, GLFontBakePixelHeight, outBitmap.data(), size, size,
                GLFontFirstChar, GLFontNumChars, outChars.data());
            return result > 0;
        }
    }

    GLFont GLFontLoader::LoadFontFromFile(const std::string& path) {
        GLFont font;

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            Log::Warn("GLFontLoader: could not open '" + path + "'");
            return font;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<unsigned char> fontData(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(fontData.data()), size)) {
            Log::Warn("GLFontLoader: failed to read '" + path + "'");
            return font;
        }

        std::vector<unsigned char> bitmap;
        std::vector<stbtt_bakedchar> chars;
        int atlasSize = 512;
        if (!TryBake(fontData, atlasSize, bitmap, chars)) {
            atlasSize = 1024;
            if (!TryBake(fontData, atlasSize, bitmap, chars)) {
                Log::Warn("GLFontLoader: '" + path + "' didn't fit even a 1024x1024 atlas, giving up");
                return font;
            }
        }

        // Expand the single-channel coverage bitmap into RGBA8 (R=G=B=255, A=coverage) --
        // matches GLTextureLoader's own RGBA8 upload format exactly, so DrawQuad's existing
        // color-modulate path (glColor4f tints the sampled texture) tints white*color=color and
        // alpha*color.a=correct coverage with zero changes to the quad-draw code itself.
        std::vector<unsigned char> rgba(static_cast<size_t>(atlasSize) * atlasSize * 4);
        for (size_t i = 0; i < bitmap.size(); i++) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = bitmap[i];
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasSize, atlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        float ascent, descent, lineGap;
        stbtt_GetScaledFontVMetrics(fontData.data(), 0, GLFontBakePixelHeight, &ascent, &descent, &lineGap);

        font.AtlasTexture = texture;
        font.AtlasWidth = atlasSize;
        font.AtlasHeight = atlasSize;
        font.Chars = std::move(chars);
        font.Ascent = ascent;
        return font;
    }

}
