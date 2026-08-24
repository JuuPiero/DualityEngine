#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"

#include <cmath>

#include <GL/glew.h>

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

        uint32_t texture = GLTextureLoader::LoadTextureFromFile(path);
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
