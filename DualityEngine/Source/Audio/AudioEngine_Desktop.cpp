#include "DualityEngine/Audio/AudioEngine.h"

#include <memory>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        ma_engine s_Engine;
        bool s_Initialized = false;

        // Every playing sound (one-shot SFX and looping music alike) is tracked here now, so
        // Volume (see AudioImportSettings) can be applied uniformly via ma_sound_set_volume --
        // the old fire-and-forget ma_engine_play_sound() path had no per-sound handle to set
        // volume on at all. A one-shot sound is reaped once finished (Update(), below); a
        // looping one lives until StopAll()/Shutdown(). unique_ptr, not a plain
        // vector<ma_sound>, since miniaudio links a playing sound into the engine's graph by its
        // own address -- std::vector growing/reallocating would move (and thus corrupt) any
        // ma_sound already registered with the engine.
        struct TrackedSound {
            std::unique_ptr<ma_sound> Sound;
            bool Loop;
        };
        std::vector<TrackedSound> s_Sounds;
    }

    void AudioEngine::Init() {
        if (ma_engine_init(nullptr, &s_Engine) != MA_SUCCESS) {
            Log::Error("AudioEngine: ma_engine_init failed");
            return;
        }
        s_Initialized = true;
    }

    void AudioEngine::Shutdown() {
        if (!s_Initialized)
            return;

        for (auto& tracked : s_Sounds) {
            ma_sound_stop(tracked.Sound.get());
            ma_sound_uninit(tracked.Sound.get());
        }
        s_Sounds.clear();

        ma_engine_uninit(&s_Engine);
        s_Initialized = false;
    }

    void AudioEngine::Update() {
        // Reclaim one-shot sounds that have finished playing -- a looping sound never reaches
        // ma_sound_at_end() on its own, so this only ever removes sounds that are truly done.
        for (size_t i = 0; i < s_Sounds.size(); ) {
            if (!s_Sounds[i].Loop && ma_sound_at_end(s_Sounds[i].Sound.get())) {
                ma_sound_uninit(s_Sounds[i].Sound.get());
                s_Sounds.erase(s_Sounds.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                i++;
            }
        }
    }

    void AudioEngine::Play(const std::string& path, bool loop, float volume) {
        if (!s_Initialized)
            return;

        auto sound = std::make_unique<ma_sound>();
        if (ma_sound_init_from_file(&s_Engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
            Log::Error("AudioEngine: failed to load '" + path + "'");
            return;
        }
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound.get());
        s_Sounds.push_back({ std::move(sound), loop });
    }

    void AudioEngine::StopAll() {
        for (auto& tracked : s_Sounds) {
            ma_sound_stop(tracked.Sound.get());
            ma_sound_uninit(tracked.Sound.get());
        }
        s_Sounds.clear();
    }

}
