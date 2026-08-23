#pragma once

#include <string>

namespace Duality {

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
    // Behaviour::PlaySound/StopAllSounds are what scripts should call
    // instead (see Scripting/EngineServices.h and Scene.cpp's adapters).
    class AudioEngine {
    public:
        static void Init();
        static void Shutdown();

        // Call once per frame -- a no-op on desktop (miniaudio runs its
        // own callback thread), present for the 3DS backend's wavebuf
        // bookkeeping and to leave room for real streaming later.
        static void Update();

        // `path` is a real file path (already resolved from an AssetRef
        // guid by the caller, e.g. Scene.cpp's EngineServices adapter) to
        // a WAV file. Fire-and-forget -- no handle returned, matching this
        // pass's "start minimal" scope (see the plan/ROADMAP for why).
        static void Play(const std::string& path, bool loop);
        static void StopAll();
    };

}
