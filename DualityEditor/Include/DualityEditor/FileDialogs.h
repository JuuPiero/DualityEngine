#pragma once

#include <string>

struct GLFWwindow;

namespace Duality {

    // Minimal native "Open File" dialog wrapper (Windows-only, matching the
    // rest of this Editor -- desktop-only, MSVC/mingw on Windows). Mirrors
    // MyGameEngine's own Engine::FileDialogs::OpenFile, scoped down to just
    // the one call DualityEditor currently needs (Open Project).
    class FileDialogs {
    public:
        // `filter` is a Windows OPENFILENAME-style double-null-terminated
        // filter string, e.g. "Duality Project (*.dproj)\0*.dproj\0".
        // Returns an empty string if the user cancels.
        static std::string OpenFile(GLFWwindow* owner, const char* filter);

        // Native "Save File" dialog (GetSaveFileNameA) -- same filter-string convention as
        // OpenFile above. `initialDir` (optional, may be null) seeds the dialog's starting
        // folder, e.g. a project's Assets directory. Prompts to overwrite if the chosen
        // file already exists. Returns an empty string if the user cancels.
        static std::string SaveFile(GLFWwindow* owner, const char* filter, const char* initialDir = nullptr);
    };

}
