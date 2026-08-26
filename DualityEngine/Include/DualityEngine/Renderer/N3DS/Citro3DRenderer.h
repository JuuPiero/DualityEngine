#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>

#include "DualityEngine/Asset/MeshLoader.h"
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

        // Must be called once after both this renderer's and Citro2DRenderer's own Init() have
        // run (see DualityPlayer::Main.cpp) -- this renderer draws into Citro2DRenderer's own
        // C2D_CreateScreenTarget targets rather than creating its own, so that whichever
        // pipeline a screen's CameraComponent::Projection currently selects, both always
        // render into (and the 3DS actually displays) the SAME target for that physical
        // screen. See Citro2DRenderer::GetTarget's own comment for the real bug this fixes.
        void SetScreenTargets(C3D_RenderTarget* top, C3D_RenderTarget* bottom);

        void BeginScene(Screen screen, ProjectionType projection, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float orthoHalfHeight, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor, bool clear) override;
        void EndScene() override;

        void DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, uint32_t subMeshIndex, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId = 0) override;
        uint32_t GetSubMeshCount(uint32_t meshHandle) const override;

        // `path` is expected to already be a citro3d-loadable ".t3x" path (romfs:/...), same
        // BuildPipeline::CookAssets/AssetDatabase::LoadManifest convention as Citro2DRenderer.
        // Loaded via Tex3DS_TextureImport (raw citro3d texturing), not citro2d's
        // C2D_SpriteSheetLoad -- a separate C3D_Tex per path, not shared with Citro2DRenderer's
        // own texture cache (a minor missed-caching opportunity if the same asset is drawn by
        // both a 2D sprite and a 3D mesh, not a correctness issue).
        uint32_t LoadTexture(const std::string& path) override;

        // `path` is expected to already be a romfs:/-resolved ".obj" path (BuildPipeline::
        // CookAssets copies non-image assets into romfs as-is, see MeshLoader.h). Parsed via
        // MeshLoader::Load (shared, cross-platform), then uploaded to a linearAlloc'd buffer
        // the same way the 3 built-in primitives are at Init() time.
        uint32_t LoadMesh(const std::string& path) override;
        void UnloadAllTextures() override;
        void UnloadAllMeshes() override;
        uint32_t GetDrawCallCount() const override { return m_DrawCallCount; }

    private:
        struct PrimitiveGpuMesh {
            void* VertexBuffer = nullptr; // linearAlloc'd
            int VertexCount = 0;
            // Submesh ranges (see MeshData::SubMesh) -- empty for the 3 built-in procedural
            // primitives, which have no material-group concept. Stored directly here (not a
            // separate parallel container) so it's freed/cleared for free by every existing
            // Shutdown()/UnloadAllMeshes() site that already handles the rest of this struct's
            // lifetime -- see OpenGLRenderer3D's GLVertexArray::SetSubMeshes for the same
            // reasoning on the desktop backend.
            std::vector<MeshData::SubMesh> SubMeshes;
        };

        C3D_RenderTarget* TargetFor(Screen screen) const;

        // Non-owning -- set by SetScreenTargets, created/destroyed by Citro2DRenderer (see
        // that method's own comment for why this renderer doesn't create its own).
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

        // Same 1-based-handle/0-reserved convention as m_Textures/m_TextureCache above, for
        // LoadMesh-imported meshes (see MeshLoader.h) -- a separate array from m_Meshes since
        // that one is fixed-size (indexed by MeshPrimitive), while this one grows per distinct
        // imported file.
        std::vector<PrimitiveGpuMesh> m_ImportedMeshes;
        std::unordered_map<std::string, uint32_t> m_MeshCache;
    };

}
