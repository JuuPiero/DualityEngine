#include "DualityEditor/ScriptEngine.h"

#include <cstdlib>
#include <filesystem>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "DualityEditor/BuildPipeline.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Scripting/ScriptModule.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"
#include "DualityEngine/Scripting/ScriptableObjectModule.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

namespace Duality {

    static HMODULE s_Module = nullptr;
    std::atomic<ReloadStatus> ScriptEngine::s_ReloadStatus{ ReloadStatus::Idle };
    std::atomic<bool> ScriptEngine::s_PendingSwap{ false };
    std::atomic<bool> ScriptEngine::s_PendingSwapSucceeded{ false };
    std::string ScriptEngine::s_PendingBuildDirectory;

    bool ScriptEngine::IsLoaded() {
        return s_Module != nullptr;
    }

    void ScriptEngine::Shutdown() {
        // All three of these MUST run before FreeLibrary below. ScriptableObjectLoader::
        // UnloadAll() calls each cached instance's Destroy function pointer, which lives inside
        // the module about to be unloaded. Less obviously, ScriptRegistry::Clear() and
        // ScriptableObjectRegistry::Clear() have the SAME hazard even though they only ever drop
        // ScriptFactoryEntry/ScriptableObjectFactoryEntry values, never call through them: each
        // entry's Fields is a std::vector<FieldHandle>, and FieldHandle::Get/Set are
        // std::function, not raw function pointers (see Field.h) -- std::function's own
        // destructor calls through a type-erasure "manager" thunk that the compiler generates
        // in whichever TU instantiated MakeField<C,T>(), i.e. inside GameScripts.dll itself.
        // Clearing the registries AFTER FreeLibrary jumps into that now-unmapped code the moment
        // a FieldHandle is destroyed. This was originally ordered the other way around (on the
        // mistaken assumption that Clear() only ever drops raw pointers) and reliably crashed
        // the Editor on the SECOND Reload Scripts of a session (the first is a no-op here since
        // s_Module starts null) -- confirmed via a live gdb repro, not guessed: SIGSEGV inside
        // ~_Function_base, called from ~FieldHandle, called from ScriptRegistry::Clear(), at an
        // address resolving into the just-freed module.
        ScriptableObjectLoader::UnloadAll();
        ScriptRegistry::Clear();
        ScriptableObjectRegistry::Clear();

        if (s_Module) {
            FreeLibrary(s_Module);
            s_Module = nullptr;
        }
    }

    bool ScriptEngine::BuildOnly(const std::string& buildDirectory) {
        // Reconfigure first so GameScripts/CMakeLists.txt's DUALITY_PROJECT_SCRIPTS_DIR GLOB
        // picks up the active project's own Scripts/ folder (new files, or a project switch
        // since the last reload) -- CMake's build graph is fixed at configure time, so a plain
        // `cmake --build` alone would never notice a script the user just created via Content
        // Browser. `-B <dir>` with no `-S` reconfigures an already-configured tree in place,
        // reading the stored source directory back out of that tree's own CMakeCache.txt.
        // Project::GetScriptsDirectory() can be relative (e.g. Application's own constructor
        // creates the default SampleProject as Project::New("SampleProject", "SampleProject"))
        // -- resolved to absolute here, unconditionally, rather than passed through as-is.
        // Confirmed via a real build failure that this matters: GameScripts/CMakeLists.txt's
        // EXISTS/file(GLOB) resolve a relative path against GameScripts/'s OWN directory, while
        // generate_fields.py (a separate process) resolves the SAME relative string against
        // wherever ITS process inherited its CWD from -- two different, inconsistent bases for
        // one string. std::filesystem::absolute() on an already-absolute path (any project
        // opened/created via a native file dialog, which always returns absolute paths) is a
        // safe no-op, so this is correct for every case, not just the relative one.
        // .generic_string() (always forward-slash, regardless of platform) rather than
        // .string() (native separator, backslashes on Windows) -- GameScripts/CMakeLists.txt's
        // own MSYS-mount-notation derivation for the 3DS build (see its own comments) matches on
        // a plain "F:/..." shape and would produce a broken result if fed backslashes instead.
        std::string projectScriptsDir;
        if (auto project = Project::GetActive())
            projectScriptsDir = std::filesystem::absolute(project->GetScriptsDirectory()).generic_string();
        std::string configureCommand = "cmake -B \"" + buildDirectory + "\" -DDUALITY_PROJECT_SCRIPTS_DIR=\"" + projectScriptsDir + "\"";
        if (std::system(configureCommand.c_str()) != 0) {
            Log::Error("ScriptEngine: reconfigure failed");
            return false;
        }

        Log::Info("ScriptEngine: rebuilding GameScripts...");
        std::string buildCommand = "cmake --build \"" + buildDirectory + "\" --target GameScripts";
        if (std::system(buildCommand.c_str()) != 0) {
            Log::Error("ScriptEngine: GameScripts build failed");
            return false;
        }
        return true;
    }

    bool ScriptEngine::SwapModule(const std::string& buildDirectory) {
        // Unload whatever is currently loaded first -- on Windows a locked
        // DLL simply fails to be overwritten by the next build otherwise.
        Shutdown();

        // GameScripts.dll lands in <build>/lib, not <build>/GameScripts -- CMake's
        // GNU/MinGW toolchain places shared-library (.dll) artifacts under a shared
        // lib\ output folder by default (mirroring Unix .so placement), unlike an
        // executable target which lands in its own target folder. Same fix as
        // run-desktop-player.bat's own PATH setup for this exact DLL.
        std::string dllPath = buildDirectory + "/lib/GameScripts.dll";
        std::string shadowPath = buildDirectory + "/lib/GameScripts.loaded.dll";
        if (!CopyFileA(dllPath.c_str(), shadowPath.c_str(), FALSE)) {
            Log::Error("ScriptEngine: could not copy '" + dllPath + "'");
            return false;
        }

        s_Module = LoadLibraryA(shadowPath.c_str());
        if (!s_Module) {
            Log::Error("ScriptEngine: failed to load '" + shadowPath + "'");
            return false;
        }

        auto getFactories = reinterpret_cast<GetScriptFactoriesFn>(GetProcAddress(s_Module, "GetScriptFactories"));
        if (!getFactories) {
            Log::Error("ScriptEngine: 'GetScriptFactories' export not found");
            Shutdown();
            return false;
        }

        const ScriptFactoryEntry* entries = nullptr;
        int count = 0;
        getFactories(&entries, &count);
        for (int i = 0; i < count; i++)
            ScriptRegistry::Register(entries[i]);

        // Optional export -- a GameScripts build with zero REGISTER_SCRIPTABLE_OBJECT'd
        // classes still exposes it (see ScriptModuleExports.cpp), so this should always be
        // found in practice, but a missing export just means "no ScriptableObject types" here
        // rather than failing the whole reload the way a missing GetScriptFactories does above.
        auto getScriptableObjectFactories = reinterpret_cast<GetScriptableObjectFactoriesFn>(
            GetProcAddress(s_Module, "GetScriptableObjectFactories"));
        int scriptableObjectCount = 0;
        if (getScriptableObjectFactories) {
            const ScriptableObjectFactoryEntry* soEntries = nullptr;
            getScriptableObjectFactories(&soEntries, &scriptableObjectCount);
            for (int i = 0; i < scriptableObjectCount; i++)
                ScriptableObjectRegistry::Register(soEntries[i]);
        }

        Log::Info("ScriptEngine: loaded " + std::to_string(count) + " script class(es), " +
            std::to_string(scriptableObjectCount) + " ScriptableObject class(es)");
        return true;
    }

    bool ScriptEngine::Reload(const std::string& buildDirectory) {
        if (!BuildOnly(buildDirectory))
            return false;
        return SwapModule(buildDirectory);
    }

    void ScriptEngine::ReloadAsync(const std::string& buildDirectory) {
        if (s_ReloadStatus == ReloadStatus::Running) {
            Log::Warn("ScriptEngine: a reload is already in progress");
            return;
        }
        if (BuildPipeline::GetStatus() == BuildStatus::Running) {
            Log::Warn("ScriptEngine: a build is already in progress, try again once it finishes");
            return;
        }
        s_ReloadStatus = ReloadStatus::Running;
        s_PendingBuildDirectory = buildDirectory;
        // Detached, not joined -- GamePanel polls GetStatus() instead of waiting on the thread.
        // The caller must have already stopped Play (synchronously, main thread) before this --
        // see this class's own header comment on why. Only BuildOnly() runs here -- the actual
        // DLL swap/registry repopulation happens later, on the main thread, via
        // PollMainThread() -- see ReloadAsync's own header comment for why (a real, reproducible
        // crash from a data race, not a hypothetical one).
        std::thread([buildDirectory]() {
            bool succeeded = BuildOnly(buildDirectory);
            s_PendingSwapSucceeded = succeeded;
            s_PendingSwap = true; // last write -- PollMainThread only reads the above after observing this true
        }).detach();
    }

    void ScriptEngine::PollMainThread() {
        if (!s_PendingSwap)
            return;
        s_PendingSwap = false;
        if (!s_PendingSwapSucceeded) {
            s_ReloadStatus = ReloadStatus::Failed;
            return;
        }
        bool succeeded = SwapModule(s_PendingBuildDirectory);
        s_ReloadStatus = succeeded ? ReloadStatus::Succeeded : ReloadStatus::Failed;
    }

}
