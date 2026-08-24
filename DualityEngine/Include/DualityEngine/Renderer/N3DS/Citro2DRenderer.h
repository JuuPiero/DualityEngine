#pragma once

#include <string>
#include <unordered_map>
#include <vector>

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

        // Not redeclaring `= true` here -- a virtual override's own default argument is
        // resolved by the STATIC type at the call site, not virtual dispatch, so relying on it
        // through an IRenderer2D& reference would silently use IRenderer2D's own default
        // instead of this one if they ever diverged. Every real call site in this codebase
        // passes `clear` explicitly regardless (see SceneRenderer.cpp's RenderScreen).
        void BeginScene(Screen screen, const glm::vec4& clearColor, bool clear) override;
        void EndScene() override;

        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees = 0.0f, uint32_t textureId = 0) override;

        // `path` is expected to already be a citro2d-loadable ".t3x" path (e.g.
        // "romfs:/Assets/Textures/foo.t3x") -- BuildPipeline::CookAssets converts every
        // project PNG to this format at "Build for 3DS" time, and AssetDatabase::LoadManifest
        // resolves an AssetRef's guid to exactly this kind of path on-device, so this never
        // needs to see a raw ".png" itself. Returns 0 (same "no texture" contract as the
        // desktop backend) if the file doesn't exist or isn't a valid ".t3x".
        uint32_t LoadTexture(const std::string& path) override;
        void UnloadAllTextures() override;
        uint32_t GetDrawCallCount() const override { return m_DrawCallCount; }

        // Exposes this renderer's own C2D_CreateScreenTarget-made targets so Citro3DRenderer
        // can draw into the exact SAME target for whichever screen goes Perspective this frame
        // (see DualityPlayer::Main.cpp's Citro3DRenderer::SetScreenTargets call). A screen's
        // CameraComponent::Projection can flip between Orthographic/Perspective at any time, so
        // both pipelines must share one C3D_RenderTarget per physical screen -- two independent
        // targets each calling C3D_RenderTargetSetOutput for the same screen would silently
        // steal each other's display output (confirmed as a real bug: the non-3D screen went
        // solid black because Citro3DRenderer's own never-cleared target had won that
        // registration race, simply by being created after this renderer's).
        C3D_RenderTarget* GetTarget(Screen screen) const { return TargetFor(screen); }

    private:
        C3D_RenderTarget* TargetFor(Screen screen) const;

        C3D_RenderTarget* m_TopTarget = nullptr;
        C3D_RenderTarget* m_BottomTarget = nullptr;
        uint32_t m_DrawCallCount = 0;

        // Index i (0-based) backs textureId i+1 -- 0 stays reserved for "none", matching
        // OpenGLRenderer2D's own handle convention. Cached by path so the same AssetRef drawn
        // by many entities only calls C2D_SpriteSheetLoad once.
        std::vector<C2D_SpriteSheet> m_TextureSheets;
        std::unordered_map<std::string, uint32_t> m_TextureCache;
    };

}
