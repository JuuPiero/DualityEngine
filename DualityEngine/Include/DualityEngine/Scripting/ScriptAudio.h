#pragma once

#include <string>

#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Fire-and-forget audio playback for scripts. This is independent of the per-entity
    // AudioSourceComponent API in AudioSource.h; use it for Unity-style one-shots that do not
    // need source state such as Pause, Loop, or IsPlaying. `assetGuid` is an AssetRef's Guid.
    class ScriptAudio {
    public:
        static void PlaySound(const std::string& assetGuid, bool loop = false) {
            const EngineServices* services = ScriptContext::Services();
            if (services)
                services->PlaySound(assetGuid.c_str(), loop);
        }

        static void StopAllSounds() {
            const EngineServices* services = ScriptContext::Services();
            if (services)
                services->StopAllSounds();
        }
    };

}
