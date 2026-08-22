#pragma once

#include <string>

namespace Duality {

    // One-click "Build for 3DS" from inside the Editor: copies the current
    // scene into DualityPlayer's romfs, then does a clean configure+build of
    // the 3DS target (producing both .3dsx and .cia, if makerom/bannertool
    // are present under Tools/).
    class BuildPipeline {
    public:
        // repoRoot: the DualityEngine repo root (parent of the desktop build dir).
        // sceneJsonPath: the scene file to package into the device build's romfs.
        static bool BuildFor3DS(const std::string& repoRoot, const std::string& sceneJsonPath);
    };

}
