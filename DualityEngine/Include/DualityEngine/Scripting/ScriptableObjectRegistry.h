#pragma once

#include <string>
#include <vector>

#include "DualityEngine/Scripting/ScriptableObjectModule.h"

namespace Duality {

    // The engine-side (host) registry of every REGISTER_SCRIPTABLE_OBJECT'd type GameScripts
    // exposes -- same population flow as ScriptRegistry (Editor: ScriptEngine::Reload, after
    // loading the DLL; DualityPlayer/DualityPlayerDesktop: read directly off the statically-
    // linked/dllimport'd GetScriptableObjectFactories() at startup). Used by the Content
    // Browser's "Create ScriptableObject" menu (needs the type list) and ScriptableObjectLoader/
    // the Properties panel (need a type's Fields to load/save/edit an actual instance).
    class ScriptableObjectRegistry {
    public:
        static void Clear();
        static void Register(const ScriptableObjectFactoryEntry& entry);
        static const ScriptableObjectFactoryEntry* Find(const std::string& className);
        static const std::vector<ScriptableObjectFactoryEntry>& All();
    };

}
