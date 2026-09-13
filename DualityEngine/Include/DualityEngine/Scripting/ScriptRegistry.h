#pragma once

#include <string>
#include <vector>

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
        static bool IsRegistered(const std::string& className);
        static bool TryCreate(const std::string& className, Behaviour** outInstance, void (**outDestroy)(Behaviour*));
        static int Count();

        // Every registered class name, e.g. for the Properties panel's "Add Script" submenu.
        // No particular order guaranteed (backed by an unordered_map).
        static std::vector<std::string> GetAllClassNames();

        // The DUALITY_PROPERTY-declared fields for `className` (empty if the class never used
        // that marker, or the class isn't registered at all) -- used by the Properties panel
        // to show/edit a script's own Inspector fields, and by Scene::OnRuntimeStart/
        // EntitySerialization to apply/persist each ScriptInstance's own PropertyOverrides.
        static const std::vector<FieldHandle>& GetFields(const std::string& className);
    };

}
