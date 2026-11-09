#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/BuiltinShaders.h"
#include "DualityEngine/Renderer/MaterialShadingMode.h"

namespace Duality {

    // A reusable rendering asset (Unity's Material): one built-in Shader (see
    // Renderer/BuiltinShaders.h) plus values for its properties and render state.
    // MeshRendererComponent / SkinnedMeshRendererComponent reference ".mat" files by GUID, so
    // several meshes share one look and editing the asset updates every mesh using it.
    // Loaded/saved by MaterialLoader (JSON, Unity property names: _Color, _MainTex, ...).
    //
    // Default-constructed values are the LEGACY look on purpose: Material{} is what an empty
    // material slot renders with, and what pre-shader ".mat" files (Color + Texture [+ Shading
    // Mode]) migrate to -- Unlit/Texture, opaque, no culling -- so existing scenes render
    // pixel-identically. New assets made from the Editor start from CreateDefaultLit() instead.
    struct Material {
        // Kept first and in this order so `Material{ color, texture }` aggregate initializers
        // written before the shader system existed still compile and mean the same thing.
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // _Color
        AssetRef Texture;                          // _MainTex; empty = white
        // Legacy mirror of the lighting choice (Unlit / VertexLit). Kept so older code and the
        // pre-shader file format keep working; Normalize() derives it from Shader. Setting it
        // to VertexLit on an Unlit-shader material selects Mobile/VertexLit (that is how old
        // files and `material.ShadingMode = VertexLit` code paths migrate).
        MaterialShadingMode ShadingMode = MaterialShadingMode::Unlit;

        BuiltinShader Shader = BuiltinShader::UnlitTexture;

        // Render state (Unity: Surface Type / Alpha Clipping, Cull, ZWrite, Render Queue).
        SurfaceType Surface = SurfaceType::Opaque; // ignored when the shader locks its surface
        CullMode Cull = CullMode::Off;             // legacy meshes were drawn double-sided
        ZWriteMode ZWrite = ZWriteMode::Auto;
        int RenderQueue = -1;                      // -1 = from the surface type
        float Cutoff = 0.5f;                       // _Cutoff, used by Cutout surfaces

        glm::vec4 MainTexST{ 1.0f, 1.0f, 0.0f, 0.0f }; // _MainTex_ST: xy tiling, zw offset
        AssetRef NormalMap;                             // _BumpMap (tangent-space normal map)

        glm::vec4 SpecColor{ 0.5f, 0.5f, 0.5f, 1.0f };  // _SpecColor
        float Shininess = 0.4f;                         // _Shininess, exponent = value * 128
        glm::vec4 EmissionColor{ 0.0f, 0.0f, 0.0f, 1.0f }; // _EmissionColor (rgb, not HDR: PICA clamps to 1)

        glm::vec4 ShadeColor{ 0.0f, 0.0f, 0.0f, 1.0f }; // _ShadeColor: Toon dark-side tint, added to ambient
        int ToonSteps = 1;                              // _ToonSteps: lit bands (1..5)
        float ToonSmoothness = 0.05f;                   // _ToonSmoothness: band edge softness (0..1)

        // Unity's "Default-Material" equivalent for brand-new assets: lit, back-face culled.
        static Material CreateDefaultLit() {
            Material material;
            material.Shader = BuiltinShader::LitDiffuse;
            material.ShadingMode = MaterialShadingMode::VertexLit;
            material.Cull = CullMode::Back;
            return material;
        }

        // Use this (not a raw `Shader =` assignment) when switching shaders on an existing
        // material: it keeps the legacy ShadingMode mirror in sync so Normalize() can't flip an
        // explicit Unlit choice back to Mobile/VertexLit.
        void SetShader(BuiltinShader shader) {
            Shader = shader;
            ShadingMode = Duality::GetShaderInfo(shader).Model == LightingModel::Unlit
                ? MaterialShadingMode::Unlit : MaterialShadingMode::VertexLit;
        }

        const ShaderInfo& GetShaderInfo() const { return Duality::GetShaderInfo(Shader); }
        LightingModel GetLightingModel() const { return GetShaderInfo().Model; }
        bool IsLit() const { return GetLightingModel() != LightingModel::Unlit; }

        SurfaceType EffectiveSurface() const {
            const ShaderInfo& info = GetShaderInfo();
            return info.SurfaceLocked ? info.LockedSurface : Surface;
        }
        bool EffectiveZWrite() const {
            if (ZWrite == ZWriteMode::On) return true;
            if (ZWrite == ZWriteMode::Off) return false;
            const SurfaceType surface = EffectiveSurface();
            return surface == SurfaceType::Opaque || surface == SurfaceType::Cutout;
        }
        int EffectiveRenderQueue() const {
            return RenderQueue >= 0 ? RenderQueue : DefaultRenderQueue(EffectiveSurface());
        }

        // Applies the legacy ShadingMode -> Shader rule and re-derives ShadingMode, clamping
        // properties to the ranges both backends support. Called by MaterialLoader on load
        // and save; harmless to call repeatedly.
        void Normalize();
    };

}
