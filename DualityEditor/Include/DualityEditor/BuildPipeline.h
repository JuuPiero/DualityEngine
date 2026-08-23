#pragma once

#include <atomic>
#include <string>

namespace Duality {

    enum class BuildStatus {
        Idle,
        Running,
        Succeeded,
        Failed
    };

    // One-click "Build for 3DS" from inside the Editor: copies the current
    // scene into DualityPlayer's romfs, then does a clean configure+build of
    // the 3DS target (producing both .3dsx and .cia, if makerom/bannertool
    // are present under Tools/).
    class BuildPipeline {
    public:
        // repoRoot: the DualityEngine repo root (parent of the desktop build dir).
        // sceneJsonPath: the scene file to package into the device build's romfs.
        // Runs synchronously (blocks the calling thread) -- see BuildFor3DSAsync
        // below for the non-blocking version MenuBarPanel actually uses; this is
        // still exposed directly for anything that genuinely wants to wait.
        static bool BuildFor3DS(const std::string& repoRoot, const std::string& sceneJsonPath);

        // Runs BuildFor3DS on a detached background thread so the Editor's UI
        // thread never blocks on it (a clean 3DS build can take up to a minute).
        // A no-op (logs and returns immediately) if a build is already Running --
        // GetStatus() is what MenuBarPanel polls to gray out the menu item and
        // avoid starting a second build concurrently. Progress/result still goes
        // through Log:: (visible in the Console panel), same as the synchronous
        // version -- Log is mutex-guarded specifically so this is safe to call
        // from a background thread while ConsolePanel reads it on the main one.
        static void BuildFor3DSAsync(const std::string& repoRoot, const std::string& sceneJsonPath);

        static BuildStatus GetStatus() { return s_Status; }

    private:
        static std::atomic<BuildStatus> s_Status;
    };

}
