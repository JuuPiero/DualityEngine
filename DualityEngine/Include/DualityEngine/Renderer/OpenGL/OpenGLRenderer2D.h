#pragma once

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

        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) override;
    };

}
