#include "DualityEngine/Renderer/RenderSettings.h"

namespace Duality {

    ShadowMode RenderSettings::s_ShadowMode = ShadowMode::BlobShadows;

    void RenderSettings::SetShadowMode(ShadowMode mode) {
        s_ShadowMode = mode;
    }

    ShadowMode RenderSettings::GetShadowMode() {
        return s_ShadowMode;
    }

    ShadowMode RenderSettings::GetEffectiveShadowMode() {
        // Individual backends may still decline ShadowMapsExperimental (the
        // 3DS renderer does and falls back to Blob Shadows), but do not erase
        // the requested policy here: desktop has a real depth-map implementation.
        return s_ShadowMode;
    }

    bool RenderSettings::UsesBlobShadows() {
        return GetEffectiveShadowMode() == ShadowMode::BlobShadows;
    }

}
