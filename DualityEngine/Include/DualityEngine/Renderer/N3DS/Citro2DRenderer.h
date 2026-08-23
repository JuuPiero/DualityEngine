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

        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees = 0.0f, uint32_t textureId = 0) override;

        // Always returns 0 -- citro2d has no PNG-loading path on real
        // hardware (needs pre-converted .t3x via the tex3ds tool, a
        // separate future asset-cooking pipeline). DrawQuad ignores
        // textureId here since it's always 0 from this backend, falling
        // back to its flat Color.
        uint32_t LoadTexture(const std::string& path) override;
        uint32_t GetDrawCallCount() const override { return m_DrawCallCount; }

    private:
        C3D_RenderTarget* TargetFor(Screen screen) const;

        C3D_RenderTarget* m_TopTarget = nullptr;
        C3D_RenderTarget* m_BottomTarget = nullptr;
        uint32_t m_DrawCallCount = 0;
    };

}
