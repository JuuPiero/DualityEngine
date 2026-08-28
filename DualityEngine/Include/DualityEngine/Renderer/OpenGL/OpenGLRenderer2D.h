#pragma once

#include <unordered_map>

#include "DualityEngine/Renderer/IRenderer2D.h"

namespace Duality {

    // Minimal desktop OpenGL implementation of IRenderer2D, used by the
    // Editor's "Play in Editor" preview. Deliberately simple (legacy
    // immediate-mode quads, no batching) for this first pass -- a batched
    // modern-GL implementation is a planned follow-up once more of the
    // renderer's real usage patterns (textures, many sprites) exist to
    // design the batching around.
    //
    // Each Screen renders through its own fixed logical resolution
    // (see Screen.h's TopScreenWidth/Height, BottomScreenWidth/Height) via an
    // orthographic projection -- the caller is responsible for binding
    // whatever render target (e.g. an FBO) and glViewport it wants the
    // 400x240 / 320x240 image rendered into before calling BeginScene.
    class OpenGLRenderer2D final : public IRenderer2D {
    public:
        void Init() override;
        void Shutdown() override;

        void BeginFrame() override;
        void EndFrame() override;

        void BeginScene(Screen screen, const glm::vec4& clearColor, bool clear) override;
        void EndScene() override;

        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees = 0.0f, uint32_t textureId = 0, const glm::vec4& uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)) override;
        uint32_t LoadTexture(const std::string& path) override;
        void UnloadAllTextures() override;
        uint32_t GetDrawCallCount() const override { return m_DrawCallCount; }

        // Desktop-editor-only extra (not part of IRenderer2D -- there is no
        // device equivalent): an orthographic view centered on an arbitrary
        // world-space point at an arbitrary zoom/viewport size, instead of
        // one of the two fixed physical screens. Backs the Editor's Scene
        // view (a free-roam camera for laying out the whole scene, like
        // Unity's Scene view), as opposed to BeginScene which always renders
        // exactly what a real CameraComponent+Screen would show (the Game
        // view).
        void BeginCustomView(const glm::vec2& center, float zoom, float viewportWidth, float viewportHeight, const glm::vec4& clearColor, bool clear = true);

        // Desktop-editor-only, same reasoning as BeginCustomView -- a Unity/Cocos-style Scene
        // view grid (world-aligned lines every `cellSize` units, plus a red X-axis/green Y-axis
        // line through the world origin) covering the area BeginCustomView's own center/zoom/
        // viewport currently frames. Must be called right after BeginCustomView and before any
        // DrawQuad calls that should render on TOP of the grid (sprites, camera markers) -- like
        // DrawQuad, this only affects the currently bound render target/projection, it doesn't
        // set either up itself.
        void DrawGrid(const glm::vec2& center, float zoom, float viewportWidth, float viewportHeight, float cellSize);

    private:
        std::unordered_map<std::string, uint32_t> m_TextureCache;
        uint32_t m_DrawCallCount = 0;
    };

}
