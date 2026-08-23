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

        void BeginScene(Screen screen, const glm::vec4& clearColor) override;
        void EndScene() override;

        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees = 0.0f, uint32_t textureId = 0) override;
        uint32_t LoadTexture(const std::string& path) override;

        // Desktop-editor-only extra (not part of IRenderer2D -- there is no
        // device equivalent): an orthographic view centered on an arbitrary
        // world-space point at an arbitrary zoom/viewport size, instead of
        // one of the two fixed physical screens. Backs the Editor's Scene
        // view (a free-roam camera for laying out the whole scene, like
        // Unity's Scene view), as opposed to BeginScene which always renders
        // exactly what a real CameraComponent+Screen would show (the Game
        // view).
        void BeginCustomView(const glm::vec2& center, float zoom, float viewportWidth, float viewportHeight, const glm::vec4& clearColor);

    private:
        std::unordered_map<std::string, uint32_t> m_TextureCache;
    };

}
