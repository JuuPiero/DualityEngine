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

        // The DUALITY_PROPERTIES-declared fields for `className` (empty if the class never
        // used that macro, or the class isn't registered at all) -- used by the Properties
        // panel to show/edit a Behaviour's own Inspector fields, and by Scene::OnRuntimeStart/
        // EntitySerialization to apply/persist BehaviourComponent::PropertyOverrides.
        static const std::vector<FieldHandle>& GetFields(const std::string& className);
    };

}
