#pragma once

namespace Duality {

    // The ABI boundary carrying engine services *into* a script -- the
    // mirror image of ScriptModule.h's ScriptFactoryEntry (which carries
    // script factories *out of* a GameScripts module into the host).
    // Deliberately raw function pointers with plain C-compatible types
    // (int, not KeyCode; const char*, not std::string) for the same reason
    // ScriptFactoryEntry is: this struct is handed to a Behaviour instance
    // that may live inside a separately-compiled GameScripts.dll on
    // desktop, so it must not depend on any C++ ABI details (name mangling
    // of enum/std::string overloads, STL layout) matching between the two
    // binaries. Built once (see Scene.cpp's s_EngineServices, wrapping the
    // real Duality::Input) and handed to every Behaviour right after it's
    // created (Scene::OnRuntimeStart) -- never constructed by a script.
    struct EngineServices {
        bool (*GetKey)(int keyCode);
        bool (*GetKeyDown)(int keyCode);
        bool (*GetKeyUp)(int keyCode);
        float (*GetAxis)(const char* axisName);
        bool (*GetPointerDown)();
        void (*GetPointerPosition)(float* outX, float* outY);

        // `assetGuid` is an AssetRef's guid (see Reflection/Field.h) -- the
        // adapter resolves it to a real file path via AssetDatabase before
        // reaching Duality::AudioEngine, same-binary calls both, exactly
        // like the texture-resolve adapters in SceneRenderer.cpp.
        void (*PlaySound)(const char* assetGuid, bool loop);
        void (*StopAllSounds)();
    };

}
