#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Duality {

    struct ProjectConfig {
        std::string Name = "Untitled";
        std::string AssetsDirectory = "Assets";
        // Relative to AssetsDirectory, matching Unity's own Assets/Scripts/ convention -- where
        // this project's own gameplay scripts live (compiled into GameScripts alongside the
        // engine's shared demo scripts, see GameScripts/CMakeLists.txt's DUALITY_PROJECT_SCRIPTS_DIR).
        std::string ScriptsDirectory = "Scripts";
        std::string StartScene;

        // Build Settings (DualityEditor/Panels/BuildSettingsPanel.cpp): Assets-relative .scene
        // paths, in build order -- index 0 is the Start Scene (the one BuildPipeline packages as
        // the device build's boot Scene.scene, and DualityPlayerDesktop's own launch reads),
        // matching Unity's own "topmost enabled scene in Build Settings" convention rather than
        // a separate start-scene picker that could desync from this list's own order. Empty means
        // "Build Settings not configured yet" -- every consumer (BuildPipeline::BuildFor3DS/
        // CookAssets, DualityPlayerDesktop/Source/Main.cpp) treats that as a no-op fallback to
        // whatever they did before this field existed, not an error.
        std::vector<std::string> ScenesInBuild;

        // Nintendo 3DS output resolve quality, selected in Build Settings and baked into
        // DualityPlayer/romfs/BuildSettings.json by BuildPipeline. 0 = native resolution,
        // 1 = 2x1 display-transfer AA, 2 = 2x2 display-transfer AA. The player falls back to
        // 0 if the requested larger render targets do not fit in VRAM.
        int N3DSAntiAliasing = 0;

        // Project Settings (DualityEditor/Panels/ProjectSettingsPanel.cpp): absolute path to a
        // PNG the user picked as this project's 3DS ".cia" icon, threaded through
        // BuildPipeline::BuildFor3DS -> build-3ds.bat -> DualityPlayer/CMakeLists.txt's
        // bannertool invocation. Empty means "not set" -- falls back to the engine's own
        // placeholder Packaging/icon.png, not an error. Desktop .exe icons are a separate,
        // unrelated toolchain (.ico + a compiled-in .rc resource) and aren't covered by this.
        std::string IconPath;

        // Project Settings: the name shown on the 3DS home menu / Citra's game list under the
        // icon above (bannertool's SMDH short/long name + makerom's APP_TITLE), threaded through
        // the exact same BuildPipeline::BuildFor3DS -> build-3ds.bat -> DualityPlayer/
        // CMakeLists.txt path as IconPath. Empty means "not set" -- BuildPipeline falls back to
        // this project's own Name field, not the engine's placeholder "DualityEngine Sample"
        // (unlike IconPath, there's no reason a real project should ship under a name that isn't
        // its own).
        std::string ProductName;

        // Physics (Project Settings). Transform/rendering stay in pixels. Box2D/Bullet
        // divide linear quantities by PPU (pixels per unit). 1 unit is not a meter —
        // any scale that keeps solver values in a comfortable range. 1 = identity.
        // Gravity is scene pixels/s^2, +Y down (default 400). Physics gravity is
        // Gravity / PPU.
        float PPU = 1.0f;
        float Gravity = 400.0f;
    };

    // Root of a Duality project on disk: an Assets folder + a config file.
    // Paths are kept as plain strings (not std::filesystem::path) since this
    // class lives in DualityEngine, which is also compiled for the 3DS
    // target where directory-enumeration-grade <filesystem> support isn't
    // relied upon -- simple string concatenation + fstream is the safer
    // common denominator.
    class Project {
    public:
        static std::shared_ptr<Project> New(const std::string& directory, const std::string& name);
        static std::shared_ptr<Project> Load(const std::string& projectFilePath);
        bool Save();

        static std::shared_ptr<Project> GetActive() { return s_ActiveProject; }

        const std::string& GetDirectory() const { return m_Directory; }
        std::string GetAssetsDirectory() const { return m_Directory + "/" + m_Config.AssetsDirectory; }
        std::string GetScriptsDirectory() const { return GetAssetsDirectory() + "/" + m_Config.ScriptsDirectory; }
        ProjectConfig& GetConfig() { return m_Config; }

    private:
        ProjectConfig m_Config;
        std::string m_Directory;
        std::string m_ProjectFilePath;

        inline static std::shared_ptr<Project> s_ActiveProject;
    };

}
