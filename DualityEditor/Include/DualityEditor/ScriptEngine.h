#pragma once

#include <atomic>
#include <string>

namespace Duality {

    enum class ReloadStatus {
        Idle,
        Running,
        Succeeded,
        Failed
    };

    // Windows-specific (LoadLibrary-based) host for the GameScripts DLL.
    // Rebuilds the module, shadow-copies it so a locked handle never blocks
    // the next rebuild, loads it, and feeds its exported factory list into
    // the shared (engine-side) ScriptRegistry that Scene::OnRuntimeStart
    // reads from.
    class ScriptEngine {
    public:
        // buildDirectory: the CMake build tree (e.g. ".../build-desktop"),
        // used both to invoke `cmake --build <dir> --target GameScripts`
        // and to locate the resulting DLL at <dir>/lib/GameScripts.dll (CMake's
        // GNU/MinGW toolchain places shared-library artifacts under lib/, not
        // the target's own subfolder).
        // Runs synchronously (blocks the calling thread) -- see ReloadAsync
        // below for the non-blocking version GamePanel actually uses; still
        // exposed directly for anything that genuinely wants to wait.
        static bool Reload(const std::string& buildDirectory);

        // Runs the reconfigure+build (the slow part) on a detached background thread so the
        // Editor's UI thread never blocks on it (same BuildFor3DSAsync/s_Status pattern
        // BuildPipeline already uses) -- but does NOT do the actual DLL swap/ScriptRegistry
        // repopulation there. That part happens later, on the MAIN thread, via PollMainThread()
        // below -- ScriptRegistry/ScriptableObjectRegistry are read every frame by the UI
        // (Properties panel's script fields, the "Add Script" submenu) with no synchronization
        // of their own, so mutating them off the main thread is unsafe in principle even though
        // it turned out NOT to be the cause of this feature's original reload crash (that was a
        // Shutdown()-ordering bug -- see its own comment -- reproducible even fully
        // single-threaded). Keeping the swap main-thread-only regardless, since the underlying
        // hazard (a concurrent read mid-mutation) is real even without a concrete repro of it. A
        // no-op (logs and returns immediately) if a reload is already Running, OR if
        // BuildPipeline reports a build Running -- ScriptEngine::Reload and BuildPipeline::
        // BuildForPC both run `cmake --build` against the SAME Ninja tree, which doesn't
        // tolerate concurrent invocations (see BuildPipeline.h's own comment on why its two
        // Async builds already share one status flag for this reason). The caller must have
        // already stopped Play synchronously on the main thread before calling this -- the
        // eventual FreeLibrary would otherwise yank GameScripts.dll out from under a live
        // Behaviour* mid-OnRuntimeUpdate.
        static void ReloadAsync(const std::string& buildDirectory);

        // Call once per frame from the main thread (Application::Run()). If a background
        // ReloadAsync build just finished, this is where the actual (fast, synchronous) DLL
        // swap + registry repopulation happens -- see ReloadAsync's own comment on why this
        // can't happen on the background thread itself.
        static void PollMainThread();

        static ReloadStatus GetStatus() { return s_ReloadStatus; }

        static void Shutdown();
        static bool IsLoaded();

    private:
        // Reconfigure + `cmake --build` only -- touches no shared C++ state (ScriptRegistry
        // etc.), so this is the part that's actually safe to run off the main thread.
        static bool BuildOnly(const std::string& buildDirectory);
        // Unload the currently-loaded module, load the freshly-built one, re-populate
        // ScriptRegistry/ScriptableObjectRegistry. MUST run on the main thread. Assumes
        // BuildOnly() already succeeded for this buildDirectory.
        static bool SwapModule(const std::string& buildDirectory);

        static std::atomic<ReloadStatus> s_ReloadStatus;
        // Set by the background thread right before it exits (s_PendingSwap last, so
        // PollMainThread observing it true establishes happens-before for the other two).
        static std::atomic<bool> s_PendingSwap;
        static std::atomic<bool> s_PendingSwapSucceeded;
        static std::string s_PendingBuildDirectory;
    };

}
