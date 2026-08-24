#pragma once

#include <string>

namespace Duality {

    // Windows-specific (LoadLibrary-based) host for the GameScripts DLL.
    // Rebuilds the module, shadow-copies it so a locked handle never blocks
    // the next rebuild, loads it, and feeds its exported factory list into
    // the shared (engine-side) ScriptRegistry that Scene::OnRuntimeStart
    // reads from.
    class ScriptEngine {
    public:
        // buildDirectory: the CMake build tree (e.g. ".../build-desktop"),
        // used both to invoke `cmake --build <dir> --target GameScripts`
        // and to locate the resulting DLL at <dir>/lib/GameScripts.dll (CMake's
        // GNU/MinGW toolchain places shared-library artifacts under lib/, not
        // the target's own subfolder).
        static bool Reload(const std::string& buildDirectory);
        static void Shutdown();
        static bool IsLoaded();
    };

}
