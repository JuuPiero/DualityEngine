#include "DualityEngine/Renderer/N3DS/Citro2DRenderer.h"

namespace Duality {

    static u32 ToC2DColor(const glm::vec4& color) {
        return C2D_Color32f(color.r, color.g, color.b, color.a);
    }

    void Citro2DRenderer::Init() {
        // C3D_Init is NOT called here -- it's process-global, singular state, owned once by
        // the app entry point (DualityPlayer::Main.cpp) now that a second, raw-citro3d
        // renderer (Citro3DRenderer) also needs it, since citro2d itself is built on top of
        // citro3d and C3D_Init must never be called twice. The caller is responsible for
        // calling C3D_Init before this Init(), same as it owns C3D_FrameBegin/FrameEnd now
        // too (see BeginFrame/EndFrame below).
        C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
        // No C2D_Prepare() here -- called at the start of every BeginScene instead, since this
        // isn't the sole GPU user (see BeginScene's own comment for why that matters).

        // Global tint state (citro2d has no per-draw mode parameter) -- TintMult (texture
        // color x tint color) matches DrawQuad's documented "a texture with color {1,1,1,1}
        // draws unmodified" contract, the same modulate semantics OpenGLRenderer2D gets for
        // free from glColor4f + a textured GL_QUADS draw.
        C2D_SetTintMode(C2D_TintMult);

        m_TopTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        m_BottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    }

    void Citro2DRenderer::Shutdown() {
        for (C2D_SpriteSheet sheet : m_TextureSheets)
            C2D_SpriteSheetFree(sheet);
        m_TextureSheets.clear();
        m_TextureCache.clear();

        C2D_Fini();
        // C3D_Fini is the caller's responsibility too, see Init()'s comment.
    }

    void Citro2DRenderer::BeginFrame() {
        // C3D_FrameBegin is the caller's responsibility (see Init()'s comment) -- it now
        // wraps BOTH this renderer's and Citro3DRenderer's screen draws for the frame,
        // whichever screens use which pipeline. This only resets this renderer's own stats.
        m_DrawCallCount = 0;
    }

    void Citro2DRenderer::EndFrame() {
        // C3D_FrameEnd is the caller's responsibility, see BeginFrame()'s comment.
    }

    C3D_RenderTarget* Citro2DRenderer::TargetFor(Screen screen) const {
        return screen == Screen::Top ? m_TopTarget : m_BottomTarget;
    }

    void Citro2DRenderer::BeginScene(Screen screen, const glm::vec4& clearColor, bool clear) {
        // C2D_Prepare's own doc comment: "This needs to be done only once in the program if
        // citro2d is the sole user of the GPU." It isn't here -- Citro3DRenderer's draws on the
        // same screen this same frame (or the previous frame; this GPU state persists across
        // C3D_FrameBegin/End) rebind citro2d's own required shader/pipeline state out from under
        // it, since citro2d itself only calls C2D_Prepare() once (this renderer's own Init()).
        // Confirmed as a real bug on real hardware/Citra: the non-3D screen went solid black
        // (citro2d silently drawing through the wrong, 3D-unlit shader) without this.
        C2D_Prepare();

        C3D_RenderTarget* target = TargetFor(screen);
        // `clear` is false when Citro3DRenderer's own BeginScene for this same screen this
        // frame already cleared it -- RenderScreen always draws a mesh pass and a sprite pass
        // into the same screen every frame now (see IRenderer3D.h's own comment), and exactly
        // one of the two should actually clear.
        if (clear)
            C2D_TargetClear(target, ToC2DColor(clearColor));
        C2D_SceneBegin(target);
    }

    void Citro2DRenderer::EndScene() {
        // citro2d submits draw calls immediately against the target selected
        // by the last C2D_SceneBegin -- nothing to flush explicitly here yet.
        // This is still a real bracket in the interface so a future batched
        // implementation has a defined place to flush from.
    }

    void Citro2DRenderer::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees, uint32_t textureId) {
        m_DrawCallCount++; // one C2D_DrawRectSolid/C2D_DrawSpriteTinted call below == one real draw call

        if (textureId != 0) {
            C2D_Sprite sprite;
            C2D_SpriteFromSheet(&sprite, m_TextureSheets[textureId - 1], 0); // single-image .t3x (BuildPipeline::CookAssets) -> always index 0
            C2D_SpriteSetCenter(&sprite, 0.5f, 0.5f); // rotate/scale around center, matching this function's own documented contract
            if (sprite.params.pos.w > 0.0f && sprite.params.pos.h > 0.0f)
                C2D_SpriteSetScale(&sprite, size.x / sprite.params.pos.w, size.y / sprite.params.pos.h); // native px -> requested Size
            glm::vec2 center = position + size * 0.5f;
            C2D_SpriteSetPos(&sprite, center.x, center.y);
            C2D_SpriteSetRotationDegrees(&sprite, rotationDegrees);

            C2D_ImageTint tint;
            C2D_PlainImageTint(&tint, ToC2DColor(color), 1.0f);
            C2D_DrawSpriteTinted(&sprite, &tint);
            return;
        }

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

    uint32_t Citro2DRenderer::LoadTexture(const std::string& path) {
        auto it = m_TextureCache.find(path);
        if (it != m_TextureCache.end())
            return it->second;

        C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(path.c_str());
        uint32_t textureId = 0;
        if (sheet) {
            m_TextureSheets.push_back(sheet);
            textureId = static_cast<uint32_t>(m_TextureSheets.size()); // 1-based, 0 reserved for "none"
        }
        m_TextureCache[path] = textureId; // cache failures too, matching OpenGLRenderer2D's own behavior
        return textureId;
    }

}
