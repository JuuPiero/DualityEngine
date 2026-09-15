#pragma once

namespace Duality {

    // Project/build-level policy for real-time mesh shadows. This is deliberately
    // independent from a material's VertexLit/Unlit choice and from a Directional
    // Light's per-light CastShadows checkbox.
    enum class ShadowMode {
        Off = 0,
        BlobShadows = 1,
        // Kept as an authored project option so the asset/configuration contract
        // is ready for a future depth-map pass. No backend implements that pass
        // yet, therefore RenderSettings resolves it to BlobShadows at runtime.
        ShadowMapsExperimental = 2
    };

    inline ShadowMode ShadowModeFromInt(int value) {
        switch (value) {
            case static_cast<int>(ShadowMode::Off):
                return ShadowMode::Off;
            case static_cast<int>(ShadowMode::ShadowMapsExperimental):
                return ShadowMode::ShadowMapsExperimental;
            default:
                return ShadowMode::BlobShadows;
        }
    }

}
