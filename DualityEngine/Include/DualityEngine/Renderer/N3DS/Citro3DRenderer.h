#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>

#include "DualityEngine/Renderer/IRenderer3D.h"

namespace Duality {

    // Raw-citro3d-backed implementation of IRenderer3D for real 3DS hardware / Citra --
    // deliberately NOT layered through citro2d (the user explicitly asked for citro3d
    // directly). Shares the same process-global C3D_Init/C3D_FrameBegin/FrameEnd bracket as
    // Citro2DRenderer, owned by the app entry point (DualityPlayer::Main.cpp) -- see
    // Citro2DRenderer::Init's own comment for why. Re-binds its shader program and
    // attribute/buffer info on every DrawMesh call rather than assuming that global C3D state
    // persists between draws: citro2d's own draws on the other screen this same frame will
    // have mutated that exact same global state in between.
    class Citro3DRenderer final : public IRenderer3D {
    public:
        void Init() override;
        void Shutdown() override;

        void BeginScene(Screen screen, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor) override;
        void EndScene() override;

        void DrawMesh(MeshPrimitive primitive, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId = 0) override;

        // `path` is expected to already be a citro3d-loadable ".t3x" path (romfs:/...), same
        // BuildPipeline::CookAssets/AssetDatabase::LoadManifest convention as Citro2DRenderer.
        // Loaded via Tex3DS_TextureImport (raw citro3d texturing), not citro2d's
        // C2D_SpriteSheetLoad -- a separate C3D_Tex per path, not shared with Citro2DRenderer's
        // own texture cache (a minor missed-caching opportunity if the same asset is drawn by
        // both a 2D sprite and a 3D mesh, not a correctness issue).
        uint32_t LoadTexture(const std::string& path) override;
        uint32_t GetDrawCallCount() const override { return m_DrawCallCount; }

    private:
        struct PrimitiveGpuMesh {
            void* VertexBuffer = nullptr; // linearAlloc'd
            int VertexCount = 0;
        };

        C3D_RenderTarget* TargetFor(Screen screen) const;

        C3D_RenderTarget* m_TopTarget = nullptr;
        C3D_RenderTarget* m_BottomTarget = nullptr;
        uint32_t m_DrawCallCount = 0;

        DVLB_s* m_ShaderDvlb = nullptr;
        shaderProgram_s m_ShaderProgram{};
        int m_UniformProjection = -1;
        int m_UniformModelView = -1;

        C3D_Mtx m_Projection{};
        C3D_Mtx m_View{}; // inverse of the camera's own world transform, set in BeginScene, read in DrawMesh

        PrimitiveGpuMesh m_Meshes[3]; // indexed by static_cast<int>(MeshPrimitive)

        // Index i (0-based) backs textureId i+1 -- 0 stays reserved for "none", matching
        // every other LoadTexture implementation's convention in this codebase.
        std::vector<C3D_Tex> m_Textures;
        std::unordered_map<std::string, uint32_t> m_TextureCache;
    };

}
