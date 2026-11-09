#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>

#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Renderer/IRenderer3D.h"

namespace Duality {

    // Per-frame PICA200 cost counters (reset by BeginScene, accumulated across screens by the
    // caller if needed). Used by DualityPlayer's optional perf log.
    struct Citro3DFrameStats {
        uint32_t DrawCalls = 0;
        uint32_t LitDrawCalls = 0;          // draws with hardware fragment lighting enabled
        uint32_t Vertices = 0;              // vertices submitted (all draws)
        uint32_t LitVertices = 0;           // vertices through mesh_lit / mesh_skin_lit
        uint32_t LightSlotUpdates = 0;      // C3D_Light reconfigurations (position/color/LUT pointers)
        uint32_t MaterialLutChanges = 0;    // D0/D1 LUT re-binds (each = a 1 KB LUT upload)
        uint32_t ProgramBinds = 0;
    };

    // Raw-citro3d-backed implementation of IRenderer3D for real 3DS hardware / Citra --
    // deliberately NOT layered through citro2d (the user explicitly asked for citro3d
    // directly). Shares the same process-global C3D_Init/C3D_FrameBegin/FrameEnd bracket as
    // Citro2DRenderer, owned by the app entry point (DualityPlayer::Main.cpp) -- see
    // Citro2DRenderer::Init's own comment for why. Re-binds its shader program and
    // attribute/buffer info on every DrawMesh call rather than assuming that global C3D state
    // persists between draws: citro2d's own draws on the other screen this same frame will
    // have mutated that exact same global state in between.
    //
    // Materials (Renderer/BuiltinShaders.h) map onto the PICA200 like this:
    //   Unlit/*, Mobile/VertexLit -> mesh.v.pica / mesh_skin.v.pica + 1 TEV stage, lighting off
    //   Lit/Diffuse, Lit/Specular -> mesh_lit.v.pica / mesh_skin_lit.v.pica + hardware fragment
    //       lighting (<= 8 lights, D0 = Blinn-Phong LUT on N.H, DA/SP LUTs per light, optional
    //       bump map on texture unit 1) + 2-3 TEV stages: albedo, albedo*primary+secondary, +emission
    //   Toon/Lit -> same programs, D1 = ramp LUT on L.N (specular1), D0 = stepped highlight
    //       on N.H, shade color as material emission, TEV albedo*(primary+secondary)+emission
    // The LUT contents come from Renderer/LightingMath.h, the same functions the desktop GLSL
    // evaluates, so both platforms produce matching images (see README "Rendering").
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
        void BeginScene(const RenderView& view, const glm::vec4& clearColor, bool clear) override;
        void EndScene() override;

        void DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, uint32_t subMeshIndex, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId = 0) override;
        void DrawMesh(const MeshDrawCommand& command) override;
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
        uint32_t CreateDynamicMesh(const MeshData& mesh) override;
        void UpdateDynamicMesh(uint32_t meshHandle, const MeshData& mesh) override;
        void UnloadAllTextures() override;
        void UnloadAllMeshes() override;
        uint32_t GetDrawCallCount() const override { return m_DrawCallCount; }

        // Counters since the last ResetFrameStats() (the player resets once per frame).
        const Citro3DFrameStats& GetFrameStats() const { return m_FrameStats; }
        void ResetFrameStats() { m_FrameStats = {}; }

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

        // One compiled .v.pica program and the uniform locations DrawMesh needs.
        struct Program {
            DVLB_s* Dvlb = nullptr;
            shaderProgram_s Shader{};
            int Projection = -1;
            int ModelView = -1;
            int NormalMatrix = -1;
            int UvTransform = -1;
            int LightVector = -1;
            int LightColor = -1;
            int AmbientColor = -1;
            int Bones = -1;
            int BoneIndexScale = -1;
        };

        void InitProgram(Program& program, const void* shbin, u32 size);
        void FreeProgram(Program& program);
        void ConfigureTexEnv(const RenderMaterial& material, bool hasTexture, const glm::vec4& tint);
        void ConfigureLighting(const MeshDrawCommand& command, const RenderMaterial& material);
        void ConfigureLightSlot(int slot, int sceneLightIndex);
        C3D_LightLut* GetMaterialLut(int kind, float a, float b, float c);
        C3D_LightLutDA* GetDistanceLut(float range);
        C3D_LightLut* GetSpotLut(float cosOuter, float cosInner);

        C3D_RenderTarget* TargetFor(Screen screen) const;

        // Non-owning -- set by SetScreenTargets, created/destroyed by Citro2DRenderer (see
        // that method's own comment for why this renderer doesn't create its own).
        C3D_RenderTarget* m_TopTarget = nullptr;
        C3D_RenderTarget* m_BottomTarget = nullptr;
        uint32_t m_DrawCallCount = 0;
        RenderView m_RenderView{};
        bool m_FlipWinding = false; // Y-mirrored orthographic projection reverses screen winding

        Program m_UnlitProgram;     // mesh.v.pica
        Program m_SkinProgram;      // mesh_skin.v.pica
        Program m_LitProgram;       // mesh_lit.v.pica
        Program m_SkinLitProgram;   // mesh_skin_lit.v.pica
        const Program* m_BoundProgram = nullptr;

        // Hardware fragment lighting: one environment, 8 light slots reconfigured per draw only
        // when the draw's light set differs from the previous draw's (sorting in SceneRenderer
        // keeps that rare). m_ViewSerial invalidates slots when the camera changes.
        C3D_LightEnv m_LightEnv{};
        C3D_Light m_Lights[MaxLightsPerDraw]{};
        int m_SlotLight[MaxLightsPerDraw]{ -1, -1, -1, -1, -1, -1, -1, -1 };
        uint32_t m_SlotSerial[MaxLightsPerDraw]{};
        bool m_SlotEnabled[MaxLightsPerDraw]{};
        uint32_t m_ViewSerial = 0;
        bool m_LightEnvBound = false;
        C3D_Material m_BoundMaterial{};
        bool m_HasBoundMaterial = false;
        C3D_LightLut* m_BoundD0 = nullptr;
        C3D_LightLut* m_BoundD1 = nullptr;
        bool m_BumpEnabled = false;
        glm::vec3 m_BoundAmbient{ -1.0f };
        std::unordered_map<uint64_t, std::unique_ptr<C3D_LightLut>> m_MaterialLuts;
        std::unordered_map<uint32_t, std::unique_ptr<C3D_LightLutDA>> m_DistanceLuts;
        std::unordered_map<uint64_t, std::unique_ptr<C3D_LightLut>> m_SpotLuts;
        int m_UsedTevStages = 1;
        Citro3DFrameStats m_FrameStats{};

        C3D_Mtx m_Projection{};
        C3D_Mtx m_View{}; // inverse of the camera's own world transform, set in BeginScene, read in DrawMesh

        PrimitiveGpuMesh m_Meshes[static_cast<int>(MeshPrimitive::Count)]; // indexed by static_cast<int>(MeshPrimitive)

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
