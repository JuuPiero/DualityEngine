#pragma once

#include <string>

#include "DualityEngine/Scripting/ScriptModule.h"

namespace Duality {

    // The engine-side (host) registry Scene::OnRuntimeStart looks scripts up
    // in by class name. Populated by whatever loaded the GameScripts module
    // (today: the desktop Editor's DLL loader, after calling the module's
    // exported GetScriptFactories()) -- this class itself has no idea
    // whether that module was a DLL or statically linked.
    class ScriptRegistry {
    public:
        static void Clear();
        static void Register(const ScriptFactoryEntry& entry);
        static bool TryCreate(const std::string& className, Behaviour** outInstance, void (**outDestroy)(Behaviour*));
        static int Count();
    };

}
