#include "DualityEngine/Project/Project.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"

#if defined(_WIN32)
#include <direct.h>
static void MakeDirectory(const std::string& path) { _mkdir(path.c_str()); }
#else
#include <sys/stat.h>
static void MakeDirectory(const std::string& path) { mkdir(path.c_str(), 0755); }
#endif

using json = nlohmann::json;

namespace Duality {

    static std::string DirectoryOf(const std::string& filePath) {
        size_t slash = filePath.find_last_of("/\\");
        return slash == std::string::npos ? "." : filePath.substr(0, slash);
    }

    std::shared_ptr<Project> Project::New(const std::string& directory, const std::string& name) {
        auto project = std::make_shared<Project>();
        project->m_Directory = directory;
        project->m_Config.Name = name;
        project->m_ProjectFilePath = directory + "/" + name + ".dproj";

        MakeDirectory(directory);
        MakeDirectory(project->GetAssetsDirectory());

        s_ActiveProject = project;
        project->Save();
        Log::Info("Created project '" + name + "' at '" + directory + "'");
        return project;
    }

    std::shared_ptr<Project> Project::Load(const std::string& projectFilePath) {
        std::ifstream file(projectFilePath);
        if (!file.is_open()) {
            Log::Error("Project: could not open '" + projectFilePath + "'");
            return nullptr;
        }

        json root;
        try {
            file >> root;
        } catch (const json::parse_error& e) {
            Log::Error("Project: failed to parse '" + projectFilePath + "': " + e.what());
            return nullptr;
        }

        auto project = std::make_shared<Project>();
        project->m_ProjectFilePath = projectFilePath;
        project->m_Directory = DirectoryOf(projectFilePath);
        project->m_Config.Name = root.value("Name", "Untitled");
        project->m_Config.AssetsDirectory = root.value("AssetsDirectory", "Assets");
        project->m_Config.ScriptsDirectory = root.value("ScriptsDirectory", "Scripts");
        project->m_Config.StartScene = root.value("StartScene", "");
        project->m_Config.ScenesInBuild = root.value("ScenesInBuild", std::vector<std::string>{});
        project->m_Config.N3DSAntiAliasing = root.value("N3DSAntiAliasing", 0);
        project->m_Config.IconPath = root.value("IconPath", "");
        project->m_Config.ProductName = root.value("ProductName", "");

        s_ActiveProject = project;
        Log::Info("Loaded project '" + project->m_Config.Name + "'");
        return project;
    }

    bool Project::Save() {
        json root;
        root["Name"] = m_Config.Name;
        root["AssetsDirectory"] = m_Config.AssetsDirectory;
        root["ScriptsDirectory"] = m_Config.ScriptsDirectory;
        root["StartScene"] = m_Config.StartScene;
        root["ScenesInBuild"] = m_Config.ScenesInBuild;
        root["N3DSAntiAliasing"] = m_Config.N3DSAntiAliasing;
        root["IconPath"] = m_Config.IconPath;
        root["ProductName"] = m_Config.ProductName;

        std::ofstream file(m_ProjectFilePath);
        if (!file.is_open()) {
            Log::Error("Project: could not write '" + m_ProjectFilePath + "'");
            return false;
        }
        file << root.dump(2);
        return true;
    }

}
