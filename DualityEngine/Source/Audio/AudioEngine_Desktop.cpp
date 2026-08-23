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

        // Looping sounds (background music) must stay alive for as long as
        // they play, unlike one-shot SFX which miniaudio's high-level
        // ma_engine_play_sound() cleans up on its own once finished.
        // unique_ptr, not a plain vector<ma_sound>, since miniaudio links a
        // playing sound into the engine's graph by its own address --
        // std::vector growing/reallocating would move (and thus corrupt)
        // any ma_sound already registered with the engine.
        std::vector<std::unique_ptr<ma_sound>> s_LoopingSounds;
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

        for (auto& sound : s_LoopingSounds) {
            ma_sound_stop(sound.get());
            ma_sound_uninit(sound.get());
        }
        s_LoopingSounds.clear();

        ma_engine_uninit(&s_Engine);
        s_Initialized = false;
    }

    void AudioEngine::Update() {
        // No-op on desktop -- miniaudio runs its own callback thread.
    }

    void AudioEngine::Play(const std::string& path, bool loop) {
        if (!s_Initialized)
            return;

        if (!loop) {
            // Fire-and-forget -- the engine owns and cleans this up once
            // it finishes playing, no handle to track.
            ma_engine_play_sound(&s_Engine, path.c_str(), nullptr);
            return;
        }

        auto sound = std::make_unique<ma_sound>();
        if (ma_sound_init_from_file(&s_Engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
            Log::Error("AudioEngine: failed to load '" + path + "'");
            return;
        }
        ma_sound_set_looping(sound.get(), MA_TRUE);
        ma_sound_start(sound.get());
        s_LoopingSounds.push_back(std::move(sound));
    }

    void AudioEngine::StopAll() {
        for (auto& sound : s_LoopingSounds) {
            ma_sound_stop(sound.get());
            ma_sound_uninit(sound.get());
        }
        s_LoopingSounds.clear();
    }

}
