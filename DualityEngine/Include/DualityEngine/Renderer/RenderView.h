#pragma once

#include <cstdint>

#include <glm/glm.hpp>

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
        glm::vec3 AmbientColor{ 0.18f };
        // Effective project runtime policy, copied once per camera pass. Keeping
        // it on RenderView makes renderer submissions deterministic for a frame.
        ShadowMode ShadowTechnique = ShadowMode::BlobShadows;
        bool HasShadowMap = false;
        ShadowMapPass ShadowMap;
        DirectionalLightData MainLight;
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
        MaterialShadingMode ShadingMode = MaterialShadingMode::Unlit;
        // AlphaBlend/DepthWrite are explicit submission state, not Material fields: the scene
        // owns transparent projected-shadow ordering while ordinary mesh materials retain their
        // legacy opaque contract. Alpha-blended draws must normally disable depth writes.
        bool AlphaBlend = false;
        bool DepthWrite = true;
    };

}
