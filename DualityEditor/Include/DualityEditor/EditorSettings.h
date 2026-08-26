#pragma once

#include <string>

namespace Duality {

    // Editor-level (cross-project) preferences -- unlike Project (per-project, lives inside the
    // project's own .dproj), this persists once per machine at
    // "%APPDATA%\DualityEngine\EditorSettings.json", the same nlohmann::json load/save idiom
    // Project::Load/Save uses. This file is DualityEditor-only (never compiled for 3DS, unlike
    // DualityEngine's Project class), so it's free to use <filesystem> without that constraint.
    class EditorSettings {
    public:
        // Lazily loads from disk on first call (a fresh install with no file yet just gets
        // defaults); the same instance is returned on every subsequent call this process.
        static EditorSettings& Get();

        bool Save();

        // Path to an external editor executable (e.g. VS Code's code.exe, Visual Studio's
        // devenv.exe) -- PreferencesPanel's "Browse..."/"Open Project in External Editor".
        // Empty until the user picks one.
        std::string ExternalEditorPath;
    };

}
