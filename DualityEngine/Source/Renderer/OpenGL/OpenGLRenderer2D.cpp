#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"

#include <cmath>

#include <GL/glew.h>

#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Renderer/OpenGL/GLTextureLoader.h"

namespace Duality {

    static void ScreenExtents(Screen screen, int& outWidth, int& outHeight) {
        if (screen == Screen::Top) {
            outWidth = TopScreenWidth;
            outHeight = TopScreenHeight;
        } else {
            outWidth = BottomScreenWidth;
            outHeight = BottomScreenHeight;
        }
    }

    void OpenGLRenderer2D::Init() {
        // Required for real text rendering (a glyph atlas is white RGB + alpha coverage,
        // tinted by DrawQuad's own color-modulate) -- this renderer never enabled blending
        // before, so alpha was previously ignored entirely (any existing content authored with
        // Color.a < 1 expecting transparency was silently rendering fully opaque). This is a
        // real, visible behavior fix, not just an addition.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void OpenGLRenderer2D::Shutdown() {
    }

    void OpenGLRenderer2D::BeginFrame() {
        m_DrawCallCount = 0;
    }

    void OpenGLRenderer2D::EndFrame() {
    }

    void OpenGLRenderer2D::BeginScene(Screen screen, const glm::vec4& clearColor, bool clear) {
        int width, height;
        ScreenExtents(screen, width, height);

        // `clear` is false when OpenGLRenderer3D's own BeginScene for this same screen this
        // frame already cleared the color buffer (see IRenderer2D.h's own doc comment) --
        // RenderScreen always draws a mesh pass and a sprite pass into the same screen every
        // frame now, and exactly one of the two should actually clear.
        if (clear) {
            glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        // Pixel-space orthographic projection, (0,0) at the top-left, matching
        // the same pixel-space convention Citro2DRenderer uses on device.
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void OpenGLRenderer2D::EndScene() {
    }

    void OpenGLRenderer2D::BeginCustomView(const glm::vec2& center, float zoom, float viewportWidth, float viewportHeight, const glm::vec4& clearColor, bool clear) {
        if (clear) {
            glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        // Same top-left-origin, Y-down convention as BeginScene, just framed
        // around an arbitrary center/zoom instead of a fixed screen size.
        float halfWidth = (viewportWidth * 0.5f) / zoom;
        float halfHeight = (viewportHeight * 0.5f) / zoom;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(center.x - halfWidth, center.x + halfWidth, center.y + halfHeight, center.y - halfHeight, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void OpenGLRenderer2D::DrawGrid(const glm::vec2& center, float zoom, float viewportWidth, float viewportHeight, float cellSize) {
        m_DrawCallCount += 2; // one glBegin/glEnd pair per pass below (plain grid, then axis lines)

        float halfWidth = (viewportWidth * 0.5f) / zoom;
        float halfHeight = (viewportHeight * 0.5f) / zoom;
        float left = center.x - halfWidth, right = center.x + halfWidth;
        float top = center.y - halfHeight, bottom = center.y + halfHeight;

        // Fully opaque, just a shade lighter than the pane's own clear color -- avoids needing
        // real alpha blending (not enabled anywhere in this legacy-immediate-mode renderer) for
        // a "faint" look; still reads clearly as subtle against Unity/Cocos-style dark panes.
        glColor4f(0.28f, 0.28f, 0.32f, 1.0f);
        glBegin(GL_LINES);
        for (float x = std::floor(left / cellSize) * cellSize; x <= right; x += cellSize) {
            glVertex2f(x, top);
            glVertex2f(x, bottom);
        }
        for (float y = std::floor(top / cellSize) * cellSize; y <= bottom; y += cellSize) {
            glVertex2f(left, y);
            glVertex2f(right, y);
        }
        glEnd();

        // World axis lines, Unity's own X=red/Y=green convention (matches the 3D pane's ground
        // grid and orientation gizmo too) -- drawn as a second pass so they're distinctly
        // colored/on top of the plain grid regardless of draw order within one glBegin/glEnd.
        glBegin(GL_LINES);
        glColor4f(0.85f, 0.25f, 0.25f, 1.0f);
        glVertex2f(left, 0.0f); glVertex2f(right, 0.0f); // X axis (world Y=0)
        glColor4f(0.25f, 0.8f, 0.3f, 1.0f);
        glVertex2f(0.0f, top); glVertex2f(0.0f, bottom); // Y axis (world X=0)
        glEnd();
    }

    void OpenGLRenderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees, uint32_t textureId, const glm::vec4& uvRect) {
        m_DrawCallCount++;

        glm::vec2 center = position + size * 0.5f;
        glm::vec2 half = size * 0.5f;

        float radians = glm::radians(rotationDegrees);
        float c = std::cos(radians), s = std::sin(radians);
        glm::vec2 localCorners[4] = { { -half.x, -half.y }, { half.x, -half.y }, { half.x, half.y }, { -half.x, half.y } };
        glm::vec2 uvs[4] = {
            { uvRect.x, uvRect.y },
            { uvRect.z, uvRect.y },
            { uvRect.z, uvRect.w },
            { uvRect.x, uvRect.w }
        };

        if (textureId != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
        }
        glColor4f(color.r, color.g, color.b, color.a);

        glBegin(GL_QUADS);
        for (int i = 0; i < 4; i++) {
            glm::vec2 rotated{ localCorners[i].x * c - localCorners[i].y * s, localCorners[i].x * s + localCorners[i].y * c };
            if (textureId != 0)
                glTexCoord2f(uvs[i].x, uvs[i].y);
            glVertex2f(center.x + rotated.x, center.y + rotated.y);
        }
        glEnd();

        if (textureId != 0) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
    }

    void OpenGLRenderer2D::DrawText(const std::string& text, const glm::vec2& position, float fontSize, const glm::vec4& color, uint32_t fontId) {
        uint32_t resolvedId = fontId != 0 ? fontId : LoadFont("");
        if (resolvedId == 0 || resolvedId > m_Fonts.size())
            return;
        const GLFont& font = m_Fonts[resolvedId - 1];
        if (font.AtlasTexture == 0)
            return;

        // Walk the string in the font's own bake-scale pixel space (cursor starts at the
        // baseline, GLFont::Ascent below the requested top-left `position`), then scale each
        // resulting glyph quad by fontSize/GLFontBakePixelHeight and offset by `position` to
        // land in real screen-pixel space -- keeps stbtt_GetBakedQuad's own coordinate math
        // (which only knows about the bake-time pixel height) decoupled from whatever size the
        // caller actually asked for.
        float scale = fontSize / GLFontBakePixelHeight;
        float x = 0.0f, y = font.Ascent;
        for (char c : text) {
            if (c < GLFontFirstChar || c >= GLFontFirstChar + GLFontNumChars)
                continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(font.Chars.data(), font.AtlasWidth, font.AtlasHeight, c - GLFontFirstChar, &x, &y, &q, 1);

            glm::vec2 glyphPos = position + glm::vec2(q.x0, q.y0) * scale;
            glm::vec2 glyphSize = glm::vec2(q.x1 - q.x0, q.y1 - q.y0) * scale;
            DrawQuad(glyphPos, glyphSize, color, 0.0f, font.AtlasTexture, { q.s0, q.t0, q.s1, q.t1 });
        }
    }

    glm::vec2 OpenGLRenderer2D::MeasureText(const std::string& text, float fontSize, uint32_t fontId) {
        uint32_t resolvedId = fontId != 0 ? fontId : LoadFont("");
        if (resolvedId == 0 || resolvedId > m_Fonts.size())
            return { 0.0f, 0.0f };
        const GLFont& font = m_Fonts[resolvedId - 1];
        if (font.AtlasTexture == 0)
            return { 0.0f, 0.0f };

        float scale = fontSize / GLFontBakePixelHeight;
        float x = 0.0f, y = font.Ascent;
        for (char c : text) {
            if (c < GLFontFirstChar || c >= GLFontFirstChar + GLFontNumChars)
                continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(font.Chars.data(), font.AtlasWidth, font.AtlasHeight, c - GLFontFirstChar, &x, &y, &q, 1);
        }
        return { x * scale, fontSize }; // height is a single-line approximation, no wrapping this pass
    }

    uint32_t OpenGLRenderer2D::LoadFont(const std::string& path) {
        // No font is vendored/shipped -- the zero-config default mirrors Citro2DRenderer's own
        // "use the platform's system font, no asset needed" fallback via a well-known OS font
        // path (this project's entire dev/build environment is Windows-only, every build script
        // in this repo assumes it).
        std::string resolvedPath = path.empty() ? "C:\\Windows\\Fonts\\segoeui.ttf" : path;

        auto it = m_FontCache.find(resolvedPath);
        if (it != m_FontCache.end())
            return it->second;

        GLFont font = GLFontLoader::LoadFontFromFile(resolvedPath);
        if (font.AtlasTexture == 0) {
            m_FontCache[resolvedPath] = 0;
            return 0;
        }

        m_Fonts.push_back(std::move(font));
        uint32_t id = static_cast<uint32_t>(m_Fonts.size()); // index+1, matching m_TextureCache's own 0-reserved convention
        m_FontCache[resolvedPath] = id;
        return id;
    }

    uint32_t OpenGLRenderer2D::LoadTexture(const std::string& path) {
        auto it = m_TextureCache.find(path);
        if (it != m_TextureCache.end())
            return it->second;

        uint32_t texture = GLTextureLoader::LoadTextureFromFile(path, TextureImportSettings::Load(path));
        m_TextureCache[path] = texture;
        return texture;
    }

    void OpenGLRenderer2D::UnloadAllTextures() {
        for (auto& [path, textureId] : m_TextureCache) {
            if (textureId != 0) {
                GLuint id = textureId;
                glDeleteTextures(1, &id);
            }
        }
        m_TextureCache.clear();
    }

    void OpenGLRenderer2D::UnloadAllFonts() {
        for (auto& font : m_Fonts) {
            if (font.AtlasTexture != 0) {
                GLuint id = font.AtlasTexture;
                glDeleteTextures(1, &id);
            }
        }
        m_Fonts.clear();
        m_FontCache.clear();
    }

}
