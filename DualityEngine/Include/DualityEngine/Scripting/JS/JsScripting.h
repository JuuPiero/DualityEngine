#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Duality {

    class Behaviour;
    class Scene;

    // Tuning for the one QuickJS runtime the engine owns. Defaults are per platform: the 3DS
    // build keeps the JS heap small and collects early (an Old 3DS application has ~64 MB in
    // total, shared with textures/meshes/audio), desktop is effectively unlimited.
    struct JsRuntimeOptions {
        size_t MemoryLimitBytes = 0;    // 0 = no limit (JS_SetMemoryLimit)
        size_t GcThresholdBytes = 0;    // 0 = QuickJS default (JS_SetGCThreshold)
        size_t MaxStackBytes = 0;       // 0 = QuickJS default (JS_SetMaxStackSize)
        // A single script callback running longer than this is interrupted (infinite loop
        // guard -- the Editor stays responsive and logs the culprit). 0 disables the watchdog.
        double CallTimeoutSeconds = 0.0;

        static JsRuntimeOptions PlatformDefault();
    };

    struct JsMemoryStats {
        size_t MallocSize = 0;    // bytes currently allocated by QuickJS
        size_t MallocLimit = 0;
        size_t ObjectCount = 0;
        size_t FunctionCount = 0;
        size_t AtomCount = 0;
    };

    // TypeScript scripting on QuickJS-ng, next to the C++ GameScripts module.
    //
    // A project's Assets/**/*.ts files are bundled ahead of time (Tools/TypeScript, esbuild)
    // into ONE classic-script JS file that already contains the `duality` API layer
    // (Tools/TypeScript/duality/duality.ts) -- the engine only provides the small native bridge
    // (`__duality`, see JsBindings.cpp). Loading the bundle registers every exported
    // MonoBehaviour subclass in ScriptRegistry (ScriptLanguage::TypeScript), so a TS script is
    // attached, serialized ("ClassName" + "PropertyOverrides", same JSON as C++ scripts),
    // shown in the Properties panel and dispatched by Scene exactly like a C++ Behaviour --
    // each instance is a JsBehaviour (a Behaviour subclass) forwarding the lifecycle into JS.
    //
    // Everything here must be called from the main thread.
    class JsScripting {
    public:
        // Loads a compiled bundle: `path` ending in ".jsbc" is QuickJS bytecode (see
        // CompileToBytecode), anything else is JavaScript source. A sibling "<path>.map"
        // (or "<bundle>.js.map" next to a .jsbc) is used to report errors at TypeScript
        // file:line. Replaces any previously loaded bundle (Unload first). Returns false (and
        // logs) on a missing file, a syntax error or an exception while registering classes.
        static bool LoadBundle(const std::string& path, const JsRuntimeOptions& options = JsRuntimeOptions::PlatformDefault());
        // Same, from memory. `displayName` is the file name stack traces show.
        static bool LoadSource(const std::string& source, const std::string& displayName,
                               const std::string& sourceMapJson = std::string(),
                               const JsRuntimeOptions& options = JsRuntimeOptions::PlatformDefault());

        // Unregisters every TypeScript class and destroys the JS runtime. Live TS instances
        // (there should be none: call it outside Play) are detached and become inert.
        static void Unload();
        static bool IsLoaded();
        static const std::string& GetLoadedPath();
        static std::vector<std::string> GetClassNames();
        // The TS source file (project-relative) a class came from, "" if unknown.
        static std::string GetClassSourceFile(const std::string& className);
        static size_t GetLiveInstanceCount();
        static JsMemoryStats GetMemoryStats();
        static void RunGC();

        // Desktop tool path: compiles a JS bundle to QuickJS bytecode (loads faster and needs
        // no parser memory on the 3DS). Uses a private runtime, safe to call any time.
        static bool CompileToBytecode(const std::string& jsPath, const std::string& outPath, std::string* outError = nullptr);

        // Evaluates `code` in the loaded context (tests / debugging). `outResult` receives
        // String(result) or the error text.
        static bool Eval(const std::string& code, std::string* outResult = nullptr);

        // Rewrites "bundle.js:line:col" positions in `text` to "Assets/...ts:line:col" using
        // the loaded source map (unchanged when there is none).
        static std::string RemapStack(const std::string& text);

        // --- Engine hooks -----------------------------------------------------------------
        // Scene::OnRuntimeStop: forget the scene's id, drop JS-side wrapper caches.
        static void OnSceneStopped(Scene* scene);
        // End of Scene::OnRuntimeUpdate (after LateUpdate, before end-of-frame destroys):
        // advances TS coroutines/invoke timers of that scene. Cheap no-op without a bundle.
        static void OnSceneFrameEnd(Scene* scene);
        // Unity's SendMessage on one TS script instance; false if `script` isn't a live TS
        // script or has no such method.
        static bool CallMethod(Behaviour* script, const char* methodName);
        // True if `script` is a live (Play-mode) TypeScript script instance.
        static bool IsTypeScriptInstance(const Behaviour* script);
    };

}
