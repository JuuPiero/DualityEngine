#pragma once

#include <cstdint>
#include <string>

namespace Duality {

    using AudioHandle = uint32_t;
    constexpr AudioHandle InvalidAudioHandle = 0;

    // Real audio playback -- a static class with platform-conditional .cpp
    // implementations (AudioEngine_Desktop.cpp / AudioEngine_3DS.cpp,
    // selected the same way OpenGLRenderer2D.cpp/Citro2DRenderer.cpp
    // already are), mirroring Duality::Input's shape rather than
    // IRenderer2D's instantiated-interface one -- there's still only ever
    // one concrete backend compiled in per target, and a static class
    // avoids needing Application/DualityPlayer::main to own an instance
    // and thread it into Scene (which nothing does for rendering either).
    //
    // GameScripts can't call this directly on desktop for the same
    // DLL-boundary reason Input needs the EngineServices bridge --
    // use ScriptAudio / AudioSource (Scripting/) instead (see EngineServices.h).
    class AudioEngine {
    public:
        static void Init();
        static void Shutdown();

        // Call once per frame -- a no-op on desktop (miniaudio runs its
        // own callback thread), present for the 3DS backend's wavebuf
        // bookkeeping and to leave room for real streaming later.
        static void Update();

        // `path` is a real file path (already resolved from an AssetRef
        // guid by the caller). Returns a handle for per-instance control
        // (Stop/SetVolume/SetPaused/IsPlaying), or InvalidAudioHandle on
        // failure. `volume` (0..1) is applied on top of the asset's own
        // AudioImportSettings::Volume (caller multiplies both).
        static AudioHandle Play(const std::string& path, bool loop, float volume = 1.0f);
        static void Stop(AudioHandle handle);
        static void StopAll();
        static void SetVolume(AudioHandle handle, float volume);
        static void SetPaused(AudioHandle handle, bool paused);
        static bool IsPlaying(AudioHandle handle);
    };

}
