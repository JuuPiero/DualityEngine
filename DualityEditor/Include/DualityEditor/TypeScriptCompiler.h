#pragma once

#include <string>

namespace Duality {

    enum class TsCompileStatus {
        Idle,
        Running,
        Succeeded,
        Failed,
    };

    // The Editor's TypeScript pipeline (the counterpart of ScriptEngine's C++ DLL reload):
    // runs Tools/TypeScript/build-scripts.mjs (node + esbuild) on the active project, reports
    // errors to the Console as "Assets/...ts:line:col: message", and (re)loads the resulting
    // bundle into JsScripting -- registering/refreshing every TS script class.
    //
    // Hot reload: Update() polls the project's *.ts files once a second; a change rebuilds in
    // the background and reloads as soon as Play isn't running (a change made during Play is
    // applied right after Stop -- live instances are never swapped mid-Play).
    class TypeScriptCompiler {
    public:
        // Startup / project open: builds and loads synchronously (no type check -- fast), so
        // scenes deserialize with every TS class already registered.
        static bool CompileAndLoadSync(const std::string& repoRoot, const std::string& projectDirectory);

        // Background build (+ type check). `force` rebuilds even if no .ts changed.
        static void CompileAsync(const std::string& repoRoot, const std::string& projectDirectory);

        // Main thread, once per frame: file watching, finishing a background build, loading
        // the new bundle when Play isn't running.
        static void Update(float deltaTime, bool isPlaying, const std::string& repoRoot, const std::string& projectDirectory);

        static TsCompileStatus GetStatus();
        static bool HasScripts(); // the project has at least one .ts file
        static std::string BundlePath(const std::string& projectDirectory);
    };

}
