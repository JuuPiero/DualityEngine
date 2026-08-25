#pragma once

#include <string>

#include "DualityEngine/Scripting/ScriptableObject.h"

namespace Duality {

    // Loads/saves ".asset" ScriptableObject instances -- same std::ifstream/nlohmann::json
    // convention as MaterialLoader, but the concrete C++ type is resolved dynamically (via
    // ScriptableObjectRegistry::Find on a "Class" field stored in the file) since a
    // ScriptableObject subclass is game-defined, not a fixed engine type like Material.
    class ScriptableObjectLoader {
    public:
        struct Loaded {
            std::string ClassName;
            // Non-owning: the loader's own internal cache (keyed by path) owns the instance for
            // the process's lifetime (or until UnloadAll), same convention as MaterialLoader's
            // own path-keyed cache -- callers never delete this. nullptr if `path` doesn't
            // exist/fails to parse, or its "Class" isn't a type ScriptableObjectRegistry knows
            // about yet (e.g. GameScripts hasn't been (re)loaded).
            ScriptableObject* Instance = nullptr;
        };

        // Cached by path -- repeated calls (every Properties panel frame, or every script read)
        // are cheap hash-map lookups after the first parse, matching MaterialLoader::Load's own
        // per-frame-safe convention.
        static Loaded Load(const std::string& path);

        // Writes every registered field of `instance` (a `className` instance, previously
        // obtained from Load/Create) to `path` as JSON, overwriting it if it already exists.
        static bool Save(const std::string& path, const std::string& className, ScriptableObject* instance);

        // Creates a brand-new default-constructed `className` instance, saves it to `path`
        // immediately, and caches it -- the Content Browser's "Create ScriptableObject" flow.
        // Returns a null Instance if `className` isn't a registered type.
        static Loaded Create(const std::string& path, const std::string& className);

        // Destroys every cached instance (via each entry's own registered Destroy function) and
        // clears the cache. MUST be called before the GameScripts module that owns those
        // instances' vtables/Destroy functions is unloaded (see ScriptEngine::Shutdown) -- same
        // dangling-pointer-into-an-unloaded-DLL hazard ScriptRegistry::Clear() avoids by never
        // calling into script code at all, except here the cache genuinely owns live objects
        // that must be destroyed while their code is still mapped in memory.
        static void UnloadAll();
    };

}
