#pragma once

// Audio components: Audio Source and Audio Listener (Audio/AudioEngine.h plays them). Part of
// Scene/Components.h.

#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // Unity's AudioSource -- per-entity clip playback with volume/loop/pause control.
    // RuntimeHandle is engine-managed (see AudioEngine.h), not serialized.
    struct AudioSourceComponent {
        bool Enabled = true;
        AssetRef Clip;
        bool Loop = false;
        float Volume = 1.0f;
        bool PlayOnAwake = false;
        bool Mute = false;
        float SpatialBlend = 0.0f;
        float MinDistance = 1.0f;
        float MaxDistance = 500.0f;

        AudioHandle RuntimeHandle = InvalidAudioHandle;
        bool Paused = false;
    };

    struct AudioListenerComponent {
        bool Enabled = true;
    };

}
