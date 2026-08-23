#include "DualityEngine/Renderer/N3DS/Citro2DRenderer.h"

namespace Duality {

    static u32 ToC2DColor(const glm::vec4& color) {
        return C2D_Color32f(color.r, color.g, color.b, color.a);
    }

    void Citro2DRenderer::Init() {
        C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
        C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
        C2D_Prepare();

        m_TopTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        m_BottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    }

    void Citro2DRenderer::Shutdown() {
        C2D_Fini();
        C3D_Fini();
    }

    void Citro2DRenderer::BeginFrame() {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        m_DrawCallCount = 0;
    }

    void Citro2DRenderer::EndFrame() {
        C3D_FrameEnd(0);
    }

    C3D_RenderTarget* Citro2DRenderer::TargetFor(Screen screen) const {
        return screen == Screen::Top ? m_TopTarget : m_BottomTarget;
    }

    void Citro2DRenderer::BeginScene(Screen screen, const glm::vec4& clearColor) {
        C3D_RenderTarget* target = TargetFor(screen);
        C2D_TargetClear(target, ToC2DColor(clearColor));
        C2D_SceneBegin(target);
    }

    void Citro2DRenderer::EndScene() {
        // citro2d submits draw calls immediately against the target selected
        // by the last C2D_SceneBegin -- nothing to flush explicitly here yet.
        // This is still a real bracket in the interface so a future batched
        // implementation has a defined place to flush from.
    }

    void Citro2DRenderer::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees, uint32_t /*textureId*/) {
        m_DrawCallCount++; // one C2D_DrawRectSolid call below == one real draw call

        if (rotationDegrees == 0.0f) {
            C2D_DrawRectSolid(position.x, position.y, 0.0f, size.x, size.y, ToC2DColor(color));
            return;
        }

        // No rotated-rect primitive in citro2d's public API -- rotate the
        // model matrix around the quad's center instead, then draw the same
        // rect centered on the local origin, matching OpenGLRenderer2D's
        // rotate-around-center behavior exactly.
        glm::vec2 center = position + size * 0.5f;
        C2D_ViewTranslate(center.x, center.y);
        C2D_ViewRotateDegrees(rotationDegrees);
        C2D_DrawRectSolid(-size.x * 0.5f, -size.y * 0.5f, 0.0f, size.x, size.y, ToC2DColor(color));
        C2D_ViewReset();
    }

    uint32_t Citro2DRenderer::LoadTexture(const std::string& /*path*/) {
        return 0;
    }

}
