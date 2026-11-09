#pragma once

#include <cstdint>
#include <string>

namespace Duality {

    // Built-in shader catalogue (Unity-style names). PICA200 has no programmable fragment
    // stage, so a "shader" here is a fixed pipeline configuration implemented twice:
    //   - desktop: one GLSL uber-shader branching on LightingModel (OpenGLRenderer3D.cpp)
    //   - 3DS:     a PICA vertex program (Assets3DS/*.v.pica) + TEV combiner stages +
    //              hardware fragment lighting LUTs (Citro3DRenderer.cpp)
    // Both implementations evaluate the SAME shared math (Renderer/LightingMath.h) so one
    // Material looks the same on both platforms. Serialized by name ("Shader": "Lit/Specular"),
    // never by enum value, so the enum order may change freely.
    enum class BuiltinShader : uint8_t {
        UnlitColor,             // "Unlit/Color"               _Color only (ignores texture + vertex colors)
        UnlitTexture,           // "Unlit/Texture"             _MainTex * _Color * vertex color (legacy Unlit)
        UnlitTransparent,       // "Unlit/Transparent"         same, alpha blended, no depth write
        UnlitCutout,            // "Unlit/Transparent Cutout"  same, alpha tested at _Cutoff
        UnlitAdditive,          // "Unlit/Additive"            same, additive blend (glows, particles)
        UnlitVertexColor,       // "Unlit/Vertex Color"        vertex color * _Color, no texture
        MobileVertexLit,        // "Mobile/VertexLit"          per-vertex Lambert, main directional light (legacy VertexLit)
        LitDiffuse,             // "Lit/Diffuse"               per-pixel Lambert, all lights, normal map, emission
        LitSpecular,            // "Lit/Specular"              + Blinn-Phong specular
        ToonLit,                // "Toon/Lit"                  banded (ramp) diffuse + hard highlight
        Count
    };

    // How the lighting term is evaluated -- this, not the shader name, is what each backend
    // switches on.
    enum class LightingModel : uint8_t {
        Unlit,       // no lights
        VertexLit,   // Gouraud: ambient + main directional Lambert, evaluated per vertex
        Lambert,     // per-pixel diffuse, up to 8 lights (PICA fragment lighting on 3DS)
        BlinnPhong,  // Lambert + specular (PICA D0 LUT = pow(N.H, exponent))
        Toon         // ramp LUT on N.L (PICA D1) + stepped highlight LUT on N.H (PICA D0)
    };

    // Unity's "Surface Type" + "Alpha Clipping" collapsed into one render-state choice.
    enum class SurfaceType : uint8_t {
        Opaque,      // Blend One Zero, ZWrite On, queue 2000
        Cutout,      // alpha test at _Cutoff, ZWrite On, queue 2450
        Transparent, // Blend SrcAlpha OneMinusSrcAlpha, ZWrite Off, queue 3000, sorted back-to-front
        Additive     // Blend SrcAlpha One, ZWrite Off, queue 3000
    };

    enum class CullMode : uint8_t { Back, Front, Off };

    // Auto = On for Opaque/Cutout, Off for Transparent/Additive (Unity's defaults).
    enum class ZWriteMode : uint8_t { Auto, On, Off };

    // Bit flags: which material properties a shader reads (drives the Material inspector).
    enum ShaderPropertyFlags : uint32_t {
        ShaderProp_Color    = 1u << 0,  // _Color
        ShaderProp_MainTex  = 1u << 1,  // _MainTex (+ tiling/offset)
        ShaderProp_BumpMap  = 1u << 2,  // _BumpMap (normal map)
        ShaderProp_Specular = 1u << 3,  // _SpecColor + _Shininess
        ShaderProp_Emission = 1u << 4,  // _EmissionColor
        ShaderProp_Toon     = 1u << 5,  // _ShadeColor, _ToonSteps, _ToonSmoothness
        ShaderProp_Cutoff   = 1u << 6,  // _Cutoff (always shown for Cutout surfaces)
    };

    struct ShaderInfo {
        BuiltinShader Id;
        const char* Name;            // serialized + shown in the Shader dropdown
        LightingModel Model;
        bool SurfaceLocked;          // true = the shader dictates its SurfaceType (Unlit/Transparent...)
        SurfaceType LockedSurface;   // only meaningful when SurfaceLocked
        bool UsesVertexColor;        // false only for Unlit/Color
        bool UsesTexture;            // false for Unlit/Color and Unlit/Vertex Color
        uint32_t Properties;         // ShaderPropertyFlags
        const char* Description;     // one line for the inspector
    };

    const ShaderInfo& GetShaderInfo(BuiltinShader shader);
    // Case-sensitive lookup by Unity-style name; returns false (and leaves `out` untouched)
    // for an unknown name.
    bool FindShaderByName(const std::string& name, BuiltinShader& out);

    // Unity render-queue defaults per surface (Geometry 2000, AlphaTest 2450, Transparent 3000).
    int DefaultRenderQueue(SurfaceType surface);

    const char* SurfaceTypeName(SurfaceType surface);
    const char* CullModeName(CullMode cull);
    const char* ZWriteModeName(ZWriteMode mode);
    bool ParseSurfaceType(const std::string& name, SurfaceType& out);
    bool ParseCullMode(const std::string& name, CullMode& out);
    bool ParseZWriteMode(const std::string& name, ZWriteMode& out);

    // Hard per-draw light limit -- PICA200's fragment lighting unit evaluates at most 8 lights;
    // the desktop shader uses the same cap so both platforms pick the same lights per object.
    constexpr int MaxLightsPerDraw = 8;

}
