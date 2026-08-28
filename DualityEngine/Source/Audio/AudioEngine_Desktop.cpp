#include "DualityEngine/Audio/AudioEngine.h"

#include <algorithm>
#include <memory>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        ma_engine s_Engine;
        bool s_Initialized = false;
        uint32_t s_NextHandle = 1;

        struct TrackedSound {
            std::unique_ptr<ma_sound> Sound;
            bool Loop = false;
            bool Paused = false;
            AudioHandle Handle = InvalidAudioHandle;
            float Volume = 1.0f;
        };
        std::vector<TrackedSound> s_Sounds;

        TrackedSound* FindTracked(AudioHandle handle) {
            for (auto& tracked : s_Sounds) {
                if (tracked.Handle == handle)
                    return &tracked;
            }
            return nullptr;
        }
    }

    void AudioEngine::Init() {
        if (ma_engine_init(nullptr, &s_Engine) != MA_SUCCESS) {
            Log::Error("AudioEngine: ma_engine_init failed");
            return;
        }
        s_Initialized = true;
        s_NextHandle = 1;
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
        for (size_t i = 0; i < s_Sounds.size(); ) {
            if (!s_Sounds[i].Loop && ma_sound_at_end(s_Sounds[i].Sound.get())) {
                ma_sound_uninit(s_Sounds[i].Sound.get());
                s_Sounds.erase(s_Sounds.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                i++;
            }
        }
    }

    AudioHandle AudioEngine::Play(const std::string& path, bool loop, float volume) {
        if (!s_Initialized)
            return InvalidAudioHandle;

        auto sound = std::make_unique<ma_sound>();
        if (ma_sound_init_from_file(&s_Engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
            Log::Error("AudioEngine: failed to load '" + path + "'");
            return InvalidAudioHandle;
        }
        ma_sound_set_volume(sound.get(), volume);
        ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(sound.get());

        AudioHandle handle = s_NextHandle++;
        if (s_NextHandle == InvalidAudioHandle)
            s_NextHandle = 1;
        s_Sounds.push_back({ std::move(sound), loop, false, handle, volume });
        return handle;
    }

    void AudioEngine::Stop(AudioHandle handle) {
        TrackedSound* tracked = FindTracked(handle);
        if (!tracked)
            return;
        ma_sound_stop(tracked->Sound.get());
        ma_sound_uninit(tracked->Sound.get());
        s_Sounds.erase(std::remove_if(s_Sounds.begin(), s_Sounds.end(),
            [handle](const TrackedSound& t) { return t.Handle == handle; }), s_Sounds.end());
    }

    void AudioEngine::StopAll() {
        for (auto& tracked : s_Sounds) {
            ma_sound_stop(tracked.Sound.get());
            ma_sound_uninit(tracked.Sound.get());
        }
        s_Sounds.clear();
    }

    void AudioEngine::SetVolume(AudioHandle handle, float volume) {
        TrackedSound* tracked = FindTracked(handle);
        if (!tracked)
            return;
        tracked->Volume = volume;
        ma_sound_set_volume(tracked->Sound.get(), volume);
    }

    void AudioEngine::SetPaused(AudioHandle handle, bool paused) {
        TrackedSound* tracked = FindTracked(handle);
        if (!tracked)
            return;
        tracked->Paused = paused;
        if (paused)
            ma_sound_stop(tracked->Sound.get());
        else
            ma_sound_start(tracked->Sound.get());
    }

    bool AudioEngine::IsPlaying(AudioHandle handle) {
        TrackedSound* tracked = FindTracked(handle);
        if (!tracked || tracked->Paused)
            return false;
        return !ma_sound_at_end(tracked->Sound.get());
    }

}
