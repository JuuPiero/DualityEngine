#pragma once

#include <string>

#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Fire-and-forget audio playback for scripts (Unity's one-shot AudioSource.Play without
    // a per-entity component yet -- see ROADMAP.md). `assetGuid` is an AssetRef's Guid.
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
