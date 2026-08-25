#pragma once

#include <string>

namespace Duality {

    // Unity's AudioImporter-equivalent, stored in the asset's ".meta" "Importer" block (same
    // convention as TextureImportSettings) -- a WAV file is a foreign binary format, same
    // reasoning as textures. Volume is the one real, wired-through setting for now: there is
    // currently no other volume control anywhere in this engine (Behaviour::PlaySound has no
    // volume parameter), so this is a genuine gap being filled, not a speculative field --
    // AudioEngine::Play applies it on both desktop (ma_sound_set_volume) and 3DS (ndsp channel
    // mix) every time this clip plays.
    struct AudioImportSettings {
        float Volume = 1.0f; // 0.0-1.0

        // Reads `assetPath`'s ".meta" "Importer" block, defaulting to full volume if missing/
        // absent -- same graceful-degradation convention as TextureImportSettings::Load.
        static AudioImportSettings Load(const std::string& assetPath);

        // Merges into `assetPath`'s ".meta" file, preserving its "guid" (and any other unrelated
        // top-level keys).
        static bool Save(const std::string& assetPath, const AudioImportSettings& settings);
    };

}
