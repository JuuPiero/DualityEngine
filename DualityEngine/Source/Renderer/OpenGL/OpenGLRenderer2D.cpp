#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"

#include <GL/glew.h>

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
    }

    void OpenGLRenderer2D::EndFrame() {
    }

    void OpenGLRenderer2D::BeginScene(Screen screen, const glm::vec4& clearColor) {
        int width, height;
        ScreenExtents(screen, width, height);

        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT);

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

    void OpenGLRenderer2D::BeginCustomView(const glm::vec2& center, float zoom, float viewportWidth, float viewportHeight, const glm::vec4& clearColor) {
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT);

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

    void OpenGLRenderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
        glColor4f(color.r, color.g, color.b, color.a);
        glBegin(GL_QUADS);
        glVertex2f(position.x, position.y);
        glVertex2f(position.x + size.x, position.y);
        glVertex2f(position.x + size.x, position.y + size.y);
        glVertex2f(position.x, position.y + size.y);
        glEnd();
    }

}
