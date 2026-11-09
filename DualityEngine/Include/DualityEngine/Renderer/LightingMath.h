#pragma once

#include <algorithm>
#include <cmath>

namespace Duality {

    // The single source of truth for every non-linear lighting curve. The 3DS backend samples
    // these functions into PICA200 fragment-lighting LUTs (256 entries, linearly interpolated
    // by the hardware); the desktop GLSL shader (OpenGLRenderer3D.cpp) evaluates the identical
    // formulas analytically. Keep the two in sync -- each GLSL helper names its C++ twin here.
    namespace LightingMath {

        inline float Saturate(float x) { return std::min(1.0f, std::max(0.0f, x)); }

        inline float SmoothStep(float edge0, float edge1, float x) {
            if (edge1 == edge0)
                return x < edge0 ? 0.0f : 1.0f;
            const float t = Saturate((x - edge0) / (edge1 - edge0));
            return t * t * (3.0f - 2.0f * t);
        }

        // Point/Spot distance attenuation, t = distance / range in [0,1]. Unity built-in
        // pipeline's 1/(1+25t^2) curve, windowed so it reaches exactly 0 at the range (the PICA
        // DA LUT clamps its input to the range, so a curve that didn't hit 0 would light
        // everything beyond it with the last LUT value). GLSL twin: dl_Attenuation.
        inline float DistanceAttenuation(float t) {
            t = Saturate(t);
            const float window = Saturate((1.0f - t) * 5.0f); // fade over the last 20% of the range
            return window * window / (1.0f + 25.0f * t * t);
        }

        // Spot cone, x = cos(angle between the spot axis and the direction to the fragment).
        // GLSL twin: dl_SpotFactor.
        inline float SpotFactor(float x, float cosOuter, float cosInner) {
            return SmoothStep(cosOuter, std::max(cosInner, cosOuter + 0.0001f), x);
        }

        // Unity legacy convention: _Shininess in (0,1] maps to a Blinn-Phong exponent of
        // _Shininess * 128.
        inline float ShininessToExponent(float shininess) {
            return std::max(1.0f, std::min(1.0f, std::max(shininess, 0.01f)) * 128.0f);
        }

        // Blinn-Phong highlight, x = N.H. GLSL twin: dl_Specular.
        inline float Specular(float x, float exponent) {
            return x <= 0.0f ? 0.0f : std::pow(x, exponent);
        }

        // Toon diffuse ramp, x = N.L. `steps` lit bands (1 = classic two-tone cel shading with the
        // terminator at N.L = 0.5); `smoothness` softens each band edge (0 = hard).
        // GLSL twin: dl_ToonRamp.
        inline float ToonRamp(float x, float steps, float smoothness) {
            x = Saturate(x);
            steps = std::max(1.0f, steps);
            const float scaled = x * steps;
            const float band = std::floor(scaled);
            const float w = std::max(0.0005f, Saturate(smoothness) * 0.5f);
            const float edge = SmoothStep(0.5f - w, 0.5f + w, scaled - band);
            return Saturate((band + edge) / steps);
        }

        // Toon hard highlight, x = N.H. GLSL twin: dl_ToonSpecular.
        inline float ToonSpecular(float x, float exponent, float smoothness) {
            const float w = std::max(0.0005f, Saturate(smoothness) * 0.5f);
            return SmoothStep(0.5f - w, 0.5f + w, Specular(x, exponent));
        }
    }

}
