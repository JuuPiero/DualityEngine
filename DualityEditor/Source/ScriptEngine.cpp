#include "DualityEditor/ScriptEngine.h"

#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scripting/ScriptModule.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    static HMODULE s_Module = nullptr;

    bool ScriptEngine::IsLoaded() {
        return s_Module != nullptr;
    }

    void ScriptEngine::Shutdown() {
        if (s_Module) {
            FreeLibrary(s_Module);
            s_Module = nullptr;
        }
        ScriptRegistry::Clear();
    }

    bool ScriptEngine::Reload(const std::string& buildDirectory) {
        Log::Info("ScriptEngine: rebuilding GameScripts...");
        std::string buildCommand = "cmake --build \"" + buildDirectory + "\" --target GameScripts";
        if (std::system(buildCommand.c_str()) != 0) {
            Log::Error("ScriptEngine: GameScripts build failed");
            return false;
        }

        // Unload whatever is currently loaded first -- on Windows a locked
        // DLL simply fails to be overwritten by the next build otherwise.
        Shutdown();

        std::string dllPath = buildDirectory + "/GameScripts/GameScripts.dll";
        std::string shadowPath = buildDirectory + "/GameScripts/GameScripts.loaded.dll";
        if (!CopyFileA(dllPath.c_str(), shadowPath.c_str(), FALSE)) {
            Log::Error("ScriptEngine: could not copy '" + dllPath + "'");
            return false;
        }

        s_Module = LoadLibraryA(shadowPath.c_str());
        if (!s_Module) {
            Log::Error("ScriptEngine: failed to load '" + shadowPath + "'");
            return false;
        }

        auto getFactories = reinterpret_cast<GetScriptFactoriesFn>(GetProcAddress(s_Module, "GetScriptFactories"));
        if (!getFactories) {
            Log::Error("ScriptEngine: 'GetScriptFactories' export not found");
            Shutdown();
            return false;
        }

        const ScriptFactoryEntry* entries = nullptr;
        int count = 0;
        getFactories(&entries, &count);
        for (int i = 0; i < count; i++)
            ScriptRegistry::Register(entries[i]);

        Log::Info("ScriptEngine: loaded " + std::to_string(count) + " script class(es)");
        return true;
    }

}
