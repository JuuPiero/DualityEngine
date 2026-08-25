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

    void OpenGLRenderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees, uint32_t textureId) {
        m_DrawCallCount++; // one glBegin/glEnd pair below == one real draw call (no batching)

        glm::vec2 center = position + size * 0.5f;
        glm::vec2 half = size * 0.5f;

        // Unrotated corners are just the four combinations of +-half offset
        // from center -- rotationDegrees==0 folds into this the same way a
        // 0-radian rotation matrix would, so there's no separate fast path
        // to keep in sync with the textured/UV logic below anymore.
        float radians = glm::radians(rotationDegrees);
        float c = std::cos(radians), s = std::sin(radians);
        glm::vec2 localCorners[4] = { { -half.x, -half.y }, { half.x, -half.y }, { half.x, half.y }, { -half.x, half.y } };
        glm::vec2 uvs[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

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

}
