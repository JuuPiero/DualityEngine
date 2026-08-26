#pragma once

#include <memory>
#include <string>

namespace Duality {

    struct ProjectConfig {
        std::string Name = "Untitled";
        std::string AssetsDirectory = "Assets";
        // Relative to AssetsDirectory, matching Unity's own Assets/Scripts/ convention -- where
        // this project's own gameplay scripts live (compiled into GameScripts alongside the
        // engine's shared demo scripts, see GameScripts/CMakeLists.txt's DUALITY_PROJECT_SCRIPTS_DIR).
        std::string ScriptsDirectory = "Scripts";
        std::string StartScene;
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
