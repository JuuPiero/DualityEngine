#include "DualityEditor/EditorSettings.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Duality {

    namespace {
        std::string SettingsFilePath() {
            const char* appData = std::getenv("APPDATA");
            if (!appData)
                return ""; // no APPDATA -- caller treats this as "can't persist", not fatal
            return std::string(appData) + "\\DualityEngine\\EditorSettings.json";
        }
    }

    EditorSettings& EditorSettings::Get() {
        static EditorSettings instance = [] {
            EditorSettings settings;
            std::string path = SettingsFilePath();
            std::ifstream file(path);
            if (file.is_open()) {
                json root;
                try {
                    file >> root;
                    settings.ExternalEditorPath = root.value("ExternalEditorPath", "");
                    settings.ColorTheme = root.value("ColorTheme", "Unity");
                    // Settings written before the Unity skin existed (no SettingsVersion) move
                    // to it once; any later explicit choice in Preferences is kept.
                    if (root.value("SettingsVersion", 1) < 2)
                        settings.ColorTheme = "Unity";
                    settings.RecentProjects = root.value("RecentProjects", std::vector<std::string>{});
                } catch (const json::parse_error&) {
                    // Corrupt/empty file -- fall through with defaults rather than fail the
                    // whole Editor over one malformed preferences file.
                }
            }
            return settings;
        }();
        return instance;
    }

    void EditorSettings::AddRecentProject(const std::string& path) {
        if (path.empty())
            return;
        const std::string normalized = std::filesystem::absolute(path).lexically_normal().string();
        RecentProjects.erase(std::remove(RecentProjects.begin(), RecentProjects.end(), normalized), RecentProjects.end());
        RecentProjects.insert(RecentProjects.begin(), normalized);
        if (RecentProjects.size() > 10)
            RecentProjects.resize(10);
    }

    bool EditorSettings::Save() {
        std::string path = SettingsFilePath();
        if (path.empty())
            return false;

        std::filesystem::create_directories(std::filesystem::path(path).parent_path());

        json root;
        root["ExternalEditorPath"] = ExternalEditorPath;
        root["ColorTheme"] = ColorTheme;
        root["RecentProjects"] = RecentProjects;
        root["SettingsVersion"] = 2;

        std::ofstream file(path);
        if (!file.is_open())
            return false;
        file << root.dump(2);
        return true;
    }

}
