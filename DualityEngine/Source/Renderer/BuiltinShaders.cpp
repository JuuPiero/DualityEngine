#include "DualityEngine/Renderer/BuiltinShaders.h"

namespace Duality {

    namespace {
        constexpr uint32_t TexColor = ShaderProp_Color | ShaderProp_MainTex;

        const ShaderInfo s_Shaders[] = {
            { BuiltinShader::UnlitColor, "Unlit/Color", LightingModel::Unlit, false, SurfaceType::Opaque,
              false, false, ShaderProp_Color,
              "Flat color. Ignores textures, vertex colors and lights." },
            { BuiltinShader::UnlitTexture, "Unlit/Texture", LightingModel::Unlit, false, SurfaceType::Opaque,
              true, true, TexColor,
              "Texture x Color x vertex color, no lighting (the legacy Unlit material)." },
            { BuiltinShader::UnlitTransparent, "Unlit/Transparent", LightingModel::Unlit, true, SurfaceType::Transparent,
              true, true, TexColor,
              "Alpha-blended Unlit/Texture; drawn after opaque geometry, back to front." },
            { BuiltinShader::UnlitCutout, "Unlit/Transparent Cutout", LightingModel::Unlit, true, SurfaceType::Cutout,
              true, true, TexColor | ShaderProp_Cutoff,
              "Alpha-tested Unlit/Texture (foliage, fences). Writes depth." },
            { BuiltinShader::UnlitAdditive, "Unlit/Additive", LightingModel::Unlit, true, SurfaceType::Additive,
              true, true, TexColor,
              "Adds texture x color to what is behind it (glows, lasers, particles)." },
            { BuiltinShader::UnlitVertexColor, "Unlit/Vertex Color", LightingModel::Unlit, false, SurfaceType::Opaque,
              true, false, ShaderProp_Color,
              "Vertex color x Color; useful for baked-lighting OBJ exports." },
            { BuiltinShader::MobileVertexLit, "Mobile/VertexLit", LightingModel::VertexLit, false, SurfaceType::Opaque,
              true, true, TexColor,
              "Cheapest lit shader: per-vertex ambient + main Directional Light (the legacy VertexLit)." },
            { BuiltinShader::LitDiffuse, "Lit/Diffuse", LightingModel::Lambert, false, SurfaceType::Opaque,
              true, true, TexColor | ShaderProp_BumpMap | ShaderProp_Emission,
              "Per-pixel Lambert, up to 8 lights, optional normal map and emission." },
            { BuiltinShader::LitSpecular, "Lit/Specular", LightingModel::BlinnPhong, false, SurfaceType::Opaque,
              true, true, TexColor | ShaderProp_BumpMap | ShaderProp_Specular | ShaderProp_Emission,
              "Lit/Diffuse + Blinn-Phong highlights (_SpecColor, _Shininess)." },
            { BuiltinShader::ToonLit, "Toon/Lit", LightingModel::Toon, false, SurfaceType::Opaque,
              true, true, TexColor | ShaderProp_Toon | ShaderProp_Specular | ShaderProp_Emission,
              "Cel shading: banded diffuse ramp + hard highlight, shade color on the dark side." },
        };
        static_assert(sizeof(s_Shaders) / sizeof(s_Shaders[0]) == static_cast<size_t>(BuiltinShader::Count),
            "every BuiltinShader needs a ShaderInfo row");
    }

    const ShaderInfo& GetShaderInfo(BuiltinShader shader) {
        const size_t index = static_cast<size_t>(shader);
        return index < static_cast<size_t>(BuiltinShader::Count) ? s_Shaders[index] : s_Shaders[static_cast<size_t>(BuiltinShader::UnlitTexture)];
    }

    bool FindShaderByName(const std::string& name, BuiltinShader& out) {
        for (const ShaderInfo& info : s_Shaders) {
            if (name == info.Name) {
                out = info.Id;
                return true;
            }
        }
        // Friendly aliases (Unity spellings people type from memory).
        if (name == "Unlit/Cutout") { out = BuiltinShader::UnlitCutout; return true; }
        if (name == "Mobile/Diffuse") { out = BuiltinShader::LitDiffuse; return true; }
        if (name == "Mobile/Specular" || name == "Mobile/Bumped Specular") { out = BuiltinShader::LitSpecular; return true; }
        if (name == "Mobile/Bumped Diffuse") { out = BuiltinShader::LitDiffuse; return true; }
        return false;
    }

    int DefaultRenderQueue(SurfaceType surface) {
        switch (surface) {
            case SurfaceType::Cutout: return 2450;
            case SurfaceType::Transparent:
            case SurfaceType::Additive: return 3000;
            default: return 2000;
        }
    }

    const char* SurfaceTypeName(SurfaceType surface) {
        switch (surface) {
            case SurfaceType::Cutout: return "Cutout";
            case SurfaceType::Transparent: return "Transparent";
            case SurfaceType::Additive: return "Additive";
            default: return "Opaque";
        }
    }

    const char* CullModeName(CullMode cull) {
        switch (cull) {
            case CullMode::Front: return "Front";
            case CullMode::Off: return "Off";
            default: return "Back";
        }
    }

    const char* ZWriteModeName(ZWriteMode mode) {
        switch (mode) {
            case ZWriteMode::On: return "On";
            case ZWriteMode::Off: return "Off";
            default: return "Auto";
        }
    }

    bool ParseSurfaceType(const std::string& name, SurfaceType& out) {
        for (SurfaceType value : { SurfaceType::Opaque, SurfaceType::Cutout, SurfaceType::Transparent, SurfaceType::Additive })
            if (name == SurfaceTypeName(value)) { out = value; return true; }
        return false;
    }

    bool ParseCullMode(const std::string& name, CullMode& out) {
        for (CullMode value : { CullMode::Back, CullMode::Front, CullMode::Off })
            if (name == CullModeName(value)) { out = value; return true; }
        return false;
    }

    bool ParseZWriteMode(const std::string& name, ZWriteMode& out) {
        for (ZWriteMode value : { ZWriteMode::Auto, ZWriteMode::On, ZWriteMode::Off })
            if (name == ZWriteModeName(value)) { out = value; return true; }
        return false;
    }

}
