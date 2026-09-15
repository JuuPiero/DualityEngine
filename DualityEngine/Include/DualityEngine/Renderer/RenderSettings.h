#pragma once

#include "DualityEngine/Renderer/ShadowMode.h"

namespace Duality {

    // Runtime copy of the project's renderer policy. Hosts set it after loading
    // a .dproj (desktop/editor) or BuildSettings.json (3DS) before rendering.
    // It lives in DualityEngine, not a player/editor, so SceneRenderer and the
    // editor's free Scene view obey exactly the same shadow decision.
    class RenderSettings {
    public:
        static void SetShadowMode(ShadowMode mode);
        static ShadowMode GetShadowMode();

        // The currently implemented hardware-safe technique. Requesting the
        // reserved shadow-map tier intentionally resolves to BlobShadows rather
        // than silently disabling all shadows or allocating an unsafe target.
        static ShadowMode GetEffectiveShadowMode();
        static bool UsesBlobShadows();

    private:
        static ShadowMode s_ShadowMode;
    };

}
