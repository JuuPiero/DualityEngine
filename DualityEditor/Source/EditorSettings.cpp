#include "DualityEditor/EditorSettings.h"

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
                } catch (const json::parse_error&) {
                    // Corrupt/empty file -- fall through with defaults rather than fail the
                    // whole Editor over one malformed preferences file.
                }
            }
            return settings;
        }();
        return instance;
    }

    bool EditorSettings::Save() {
        std::string path = SettingsFilePath();
        if (path.empty())
            return false;

        std::filesystem::create_directories(std::filesystem::path(path).parent_path());

        json root;
        root["ExternalEditorPath"] = ExternalEditorPath;

        std::ofstream file(path);
        if (!file.is_open())
            return false;
        file << root.dump(2);
        return true;
    }

}
