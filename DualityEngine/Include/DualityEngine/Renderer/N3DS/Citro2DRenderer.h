#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "DualityEngine/Renderer/IRenderer2D.h"

namespace Duality {

    // citro2d-backed implementation of IRenderer2D for real 3DS hardware /
    // Citra. Always creates both physical screen render targets up front --
    // that is simply how citro2d bring-up works, regardless of whether
    // gameplay uses the bottom screen yet.
    class Citro2DRenderer final : public IRenderer2D {
    public:
        void Init() override;
        void Shutdown() override;

        void BeginFrame() override;
        void EndFrame() override;

        void BeginScene(Screen screen, const glm::vec4& clearColor) override;
        void EndScene() override;

        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) override;

    private:
        C3D_RenderTarget* TargetFor(Screen screen) const;

        C3D_RenderTarget* m_TopTarget = nullptr;
        C3D_RenderTarget* m_BottomTarget = nullptr;
    };

}
