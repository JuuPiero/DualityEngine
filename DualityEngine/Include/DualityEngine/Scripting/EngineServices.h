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

        // Entity lookup is inherently per-Scene (unlike Input/Audio, which are
        // engine-wide singletons), so `scene` travels as a parameter each call
        // rather than being baked into this one shared struct instance -- pass
        // Behaviour::GetEntity().GetScene() (opaque here on purpose, same
        // reasoning as everywhere else in this file: no engine type in the ABI).
        // `screen` is a Duality::Screen cast to int (0=Top, 1=Bottom). Returns
        // true and fills *outHandle (the raw entt::entity value) if found.
        bool (*FindEntityInScreen)(void* scene, int screen, const char* name, unsigned int* outHandle);

        // GameScripts can't call Duality::Log directly (same DLL-boundary reason it can't
        // call Duality::Input/AudioEngine directly either) -- these route through it instead,
        // showing up in the Editor's Console panel exactly like every other engine log line.
        void (*LogInfo)(const char* message);
        void (*LogWarn)(const char* message);
        void (*LogError)(const char* message);

        // Unity's SceneManager.LoadScene -- `assetsRelativePath` is resolved against
        // whichever platform's own Assets root is currently running (see SceneManager.h's
        // own comment for why this is a deferred request, not an immediate swap).
        void (*RequestLoadScene)(const char* assetsRelativePath);

        // Unity's Object.Instantiate -- `scene` is the calling Behaviour's own
        // GetEntity().GetScene() (same per-call-not-baked-in reasoning as
        // FindEntityInScreen above, since Instantiate needs the live Scene the SAME
        // instant it's called, unlike LoadScene's deferred swap). `prefabAssetGuid` is
        // an AssetRef's Guid (a ".prefab" asset). Returns true and fills
        // *outHandle with the new root entity's raw handle on success.
        bool (*Instantiate)(void* scene, const char* prefabAssetGuid, unsigned int* outHandle);

        // Unity's ScriptableObject data-asset lookup by AssetRef guid -- the adapter resolves
        // the guid to a real ".asset" path via AssetDatabase and hands back the cached
        // instance from ScriptableObjectLoader, same resolve-then-same-binary-call shape as
        // PlaySound above. Returns an opaque pointer (nullptr if unresolved) rather than a
        // ScriptableObject* by name -- same no-engine-type-in-the-ABI reasoning as everywhere
        // else in this struct; Behaviour::LoadScriptableObject<T>() below does the cast, since
        // only the calling script knows which concrete GameScripts-side type it wants.
        void* (*LoadScriptableObject)(const char* assetGuid);
    };

}
