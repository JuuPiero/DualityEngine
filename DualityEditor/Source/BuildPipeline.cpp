#include "DualityEditor/BuildPipeline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
#include <thread>

#include "DualityEngine/Core/Log.h"

namespace Duality {

    std::atomic<BuildStatus> BuildPipeline::s_Status{ BuildStatus::Idle };

    void BuildPipeline::BuildFor3DSAsync(const std::string& repoRoot, const std::string& sceneJsonPath) {
        if (s_Status == BuildStatus::Running) {
            Log::Warn("BuildPipeline: a build is already in progress");
            return;
        }
        s_Status = BuildStatus::Running;
        // Detached, not joined -- MenuBarPanel polls GetStatus() instead of
        // waiting on the thread. Closing the Editor mid-build leaves the
        // build-3ds.bat child process tree to finish or get cleaned up on its
        // own (no job-object-based process tracking here) -- an accepted, rare
        // edge case, not a normal shutdown path.
        std::thread([repoRoot, sceneJsonPath]() {
            bool succeeded = BuildFor3DS(repoRoot, sceneJsonPath);
            s_Status = succeeded ? BuildStatus::Succeeded : BuildStatus::Failed;
        }).detach();
    }

    bool BuildPipeline::BuildFor3DS(const std::string& repoRoot, const std::string& sceneJsonPath) {
        std::string sceneDest = repoRoot + "\\DualityPlayer\\romfs\\Scene.json";
        if (!CopyFileA(sceneJsonPath.c_str(), sceneDest.c_str(), FALSE)) {
            Log::Error("BuildPipeline: could not copy scene to '" + sceneDest + "'");
            return false;
        }

        Log::Info("BuildPipeline: building for 3DS (clean build, can take up to a minute)...");

        // Delegates to build-3ds.bat (a real, separately-parsed script file)
        // rather than hand-building a chained cmd.exe command string here --
        // std::system() already wraps its argument in its own "cmd.exe /c
        // "..."", so an extra self-authored "cmd.exe /c \"...\"" layer around
        // a string that itself contains quoted sub-arguments produces
        // mis-nested quotes that cmd.exe silently mis-parses (this broke
        // the first version of this function). A single quoted path to an
        // independent .bat file has none of that hazard, and keeps exactly
        // one place (build-3ds.bat) that knows how to configure/build the
        // 3DS target -- everything a plain double-click of that script does
        // is exactly what this button does.
        std::string command = "\"" + repoRoot + "\\build-3ds.bat\"";
        if (std::system(command.c_str()) != 0) {
            Log::Error("BuildPipeline: 3DS build failed");
            return false;
        }

        Log::Info("BuildPipeline: built " + repoRoot + "\\build-3ds\\DualityPlayer\\DualityPlayer.3dsx (+ .cia if makerom/bannertool are present under Tools/)");
        return true;
    }

}
