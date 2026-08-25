#include "DualityEditor/ScriptEngine.h"

#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scripting/ScriptModule.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"
#include "DualityEngine/Scripting/ScriptableObjectModule.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

namespace Duality {

    static HMODULE s_Module = nullptr;

    bool ScriptEngine::IsLoaded() {
        return s_Module != nullptr;
    }

    void ScriptEngine::Shutdown() {
        // MUST run before FreeLibrary below -- these cached ScriptableObject instances' vtables
        // and Destroy function pointers live inside the module about to be unloaded, so calling
        // Destroy on them afterward would jump into unmapped memory. ScriptRegistry::Clear()/
        // ScriptableObjectRegistry::Clear() don't have this hazard (they only drop raw function
        // pointers, never call through them), so their ordering relative to FreeLibrary doesn't
        // matter -- only the cache's owned instances need to be torn down first.
        ScriptableObjectLoader::UnloadAll();

        if (s_Module) {
            FreeLibrary(s_Module);
            s_Module = nullptr;
        }
        ScriptRegistry::Clear();
        ScriptableObjectRegistry::Clear();
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

        // GameScripts.dll lands in <build>/lib, not <build>/GameScripts -- CMake's
        // GNU/MinGW toolchain places shared-library (.dll) artifacts under a shared
        // lib\ output folder by default (mirroring Unix .so placement), unlike an
        // executable target which lands in its own target folder. Same fix as
        // run-desktop-player.bat's own PATH setup for this exact DLL.
        std::string dllPath = buildDirectory + "/lib/GameScripts.dll";
        std::string shadowPath = buildDirectory + "/lib/GameScripts.loaded.dll";
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

        // Optional export -- a GameScripts build with zero REGISTER_SCRIPTABLE_OBJECT'd
        // classes still exposes it (see ScriptModuleExports.cpp), so this should always be
        // found in practice, but a missing export just means "no ScriptableObject types" here
        // rather than failing the whole reload the way a missing GetScriptFactories does above.
        auto getScriptableObjectFactories = reinterpret_cast<GetScriptableObjectFactoriesFn>(
            GetProcAddress(s_Module, "GetScriptableObjectFactories"));
        int scriptableObjectCount = 0;
        if (getScriptableObjectFactories) {
            const ScriptableObjectFactoryEntry* soEntries = nullptr;
            getScriptableObjectFactories(&soEntries, &scriptableObjectCount);
            for (int i = 0; i < scriptableObjectCount; i++)
                ScriptableObjectRegistry::Register(soEntries[i]);
        }

        Log::Info("ScriptEngine: loaded " + std::to_string(count) + " script class(es), " +
            std::to_string(scriptableObjectCount) + " ScriptableObject class(es)");
        return true;
    }

}
