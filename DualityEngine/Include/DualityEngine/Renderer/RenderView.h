#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/BuiltinShaders.h"
#include "DualityEngine/Renderer/MaterialShadingMode.h"
#include "DualityEngine/Renderer/MeshPrimitive.h"
#include "DualityEngine/Renderer/ProjectionType.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Renderer/ShadowMode.h"

namespace Duality {

    // Scene-owned camera/light selection, constructed once per primary-camera pass.
    // It deliberately contains no asset paths or backend handles.
    struct DirectionalLightData {
        bool Enabled = false;
        glm::vec3 Direction{ 0.0f, -1.0f, 0.0f }; // from shaded point toward the light
        glm::vec3 Color{ 1.0f };
        float Intensity = 1.0f;
        bool CastShadows = false;
    };

    enum class LightType : uint8_t { Directional, Point, Spot };

    // One resolved scene light in world space, shared by every draw of a RenderView. Draws
    // reference up to MaxLightsPerDraw of these by index (MeshDrawCommand::LightIndices).
    struct SceneLight {
        LightType Type = LightType::Directional;
        glm::vec3 Position{ 0.0f };
        // Directional: from the shaded point TOWARD the light (same as DirectionalLightData).
        // Spot: the direction the cone points (entity forward, -Z). Point: unused.
        glm::vec3 Direction{ 0.0f, 1.0f, 0.0f };
        // Color * Intensity, saturated to [0,1] per channel -- PICA200 light color registers
        // are 8-bit, and the desktop shader clamps identically so both platforms match.
        glm::vec3 Color{ 1.0f };
        float Range = 10.0f;
        float CosOuter = 0.0f;  // cos(SpotAngle / 2)
        float CosInner = 0.0f;  // cos(InnerSpotAngle / 2)
        bool CastShadows = false;
    };

    // Light-space camera for an optional depth shadow map. The scene renderer
    // owns its bounds/policy; a backend merely allocates/renders/samples it.
    struct ShadowMapPass {
        glm::mat4 ViewProjection{ 1.0f };
    };

    struct RenderView {
        Screen TargetScreen = Screen::Top;
        ProjectionType Projection = ProjectionType::Orthographic;
        glm::vec3 CameraPosition{ 0.0f };
        glm::vec3 CameraRotationDegrees{ 0.0f };
        float FovDegrees = 60.0f;
        float OrthoHalfHeight = 120.0f;
        float AspectRatio = 1.0f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        // Scene ambient (LightingSettings::EffectiveAmbient). Default = the pre-Lighting-window
        // hard-coded value, so code that builds a RenderView by hand keeps the old look.
        glm::vec3 AmbientColor{ 0.18f };
        // Effective project runtime policy, copied once per camera pass. Keeping
        // it on RenderView makes renderer submissions deterministic for a frame.
        ShadowMode ShadowTechnique = ShadowMode::BlobShadows;
        bool HasShadowMap = false;
        ShadowMapPass ShadowMap;
        // The first enabled Directional Light in hierarchy order: drives Mobile/VertexLit,
        // blob shadows and the desktop shadow map.
        DirectionalLightData MainLight;
        // Every enabled light of the scene (Directional, Point, Spot), main light first.
        std::vector<SceneLight> Lights;
        int MainLightIndex = -1; // index into Lights, -1 = no directional light
    };

    // Fully-resolved material for one draw. SceneRenderer resolves the Material asset
    // (textures -> backend handles, shininess -> exponent, surface -> blend/zwrite); backends
    // only translate it into GPU state (GLSL uniforms / PICA TEV + light env).
    struct RenderMaterial {
        BuiltinShader Shader = BuiltinShader::UnlitTexture;
        LightingModel Model = LightingModel::Unlit;
        SurfaceType Surface = SurfaceType::Opaque;
        CullMode Cull = CullMode::Off;
        bool ZWrite = true;
        bool UseVertexColor = true;
        bool UseTexture = true;
        uint32_t NormalTextureId = 0;        // 0 = no normal map
        glm::vec4 MainTexST{ 1.0f, 1.0f, 0.0f, 0.0f };
        float Cutoff = 0.5f;
        glm::vec3 SpecColor{ 0.0f };
        float SpecExponent = 51.2f;          // LightingMath::ShininessToExponent(_Shininess)
        glm::vec3 Emission{ 0.0f };
        glm::vec3 ShadeColor{ 0.0f };
        float ToonSteps = 1.0f;
        float ToonSmoothness = 0.05f;
    };

    // Fully-resolved submission unit. SceneRenderer resolves assets and chooses the material;
    // backends only bind handles and issue GPU work.
    struct MeshDrawCommand {
        MeshPrimitive Primitive = MeshPrimitive::Cube;
        uint32_t MeshHandle = 0;
        uint32_t SubMeshIndex = 0;
        glm::vec3 Translation{ 0.0f };
        glm::vec3 RotationDegrees{ 0.0f };
        glm::vec3 Scale{ 1.0f };
        glm::vec4 Color{ 1.0f };
        uint32_t TextureId = 0;
        // Legacy shading/blend description, used only when HasMaterial is false (editor
        // overlays, blob shadows, code written before the shader system): Unlit or VertexLit,
        // opaque or alpha-blended, never culled.
        MaterialShadingMode ShadingMode = MaterialShadingMode::Unlit;
        bool AlphaBlend = false;
        bool DepthWrite = true;
        // Non-owning, synchronous skinning palette. SceneRenderer builds this once when a
        // character's pose changes; a backend consumes it during this DrawMesh call only.
        // Null/zero preserves the regular static-mesh path exactly.
        const glm::mat4* SkinMatrices = nullptr;
        uint32_t SkinMatrixCount = 0;

        // Full material description (shader, render state, lighting parameters). When false
        // the backend derives an equivalent RenderMaterial from the legacy fields above.
        bool HasMaterial = false;
        RenderMaterial Material;
        // Lights affecting this draw: indices into RenderView::Lights, most important first.
        uint8_t LightCount = 0;
        uint8_t LightIndices[MaxLightsPerDraw]{};
    };

    // The RenderMaterial a legacy (HasMaterial == false) command stands for.
    inline RenderMaterial ResolveCommandMaterial(const MeshDrawCommand& command) {
        if (command.HasMaterial)
            return command.Material;
        RenderMaterial material;
        material.Model = command.ShadingMode == MaterialShadingMode::VertexLit ? LightingModel::VertexLit : LightingModel::Unlit;
        material.Shader = material.Model == LightingModel::VertexLit ? BuiltinShader::MobileVertexLit : BuiltinShader::UnlitTexture;
        material.Surface = command.AlphaBlend ? SurfaceType::Transparent : SurfaceType::Opaque;
        material.ZWrite = command.DepthWrite;
        material.Cull = CullMode::Off;
        return material;
    }

}
