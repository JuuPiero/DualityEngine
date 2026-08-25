#pragma once

#include <vector>

#include "DualityEngine/Scripting/ScriptableObjectModule.h"

// Self-registration machinery living entirely inside this module's own binary, same reasoning
// (and same shape) as ScriptRegistration.h's GetLocalScriptFactories/ScriptRegistrar --
// see ScriptModuleExports.cpp for how the resulting list crosses back out to the host.
namespace Duality {

    inline std::vector<ScriptableObjectFactoryEntry>& GetLocalScriptableObjectFactories() {
        static std::vector<ScriptableObjectFactoryEntry> factories;
        return factories;
    }

    template<typename T>
    struct ScriptableObjectRegistrar {
        explicit ScriptableObjectRegistrar(const char* name) {
            GetLocalScriptableObjectFactories().push_back(ScriptableObjectFactoryEntry{
                name,
                []() -> ScriptableObject* { return new T(); },
                [](ScriptableObject* instance) { delete instance; },
                T::Fields(),
            });
        }
    };

}

// `ClassName` must define `static std::vector<Duality::FieldHandle> Fields()` listing its
// reflected fields via MakeField(), the same way Reflection.cpp declares built-in component
// fields -- there's no per-Entity TypeRegistry entry to hang this off of for a ScriptableObject.
#define REGISTER_SCRIPTABLE_OBJECT(ClassName) \
    static Duality::ScriptableObjectRegistrar<ClassName> s_ScriptableObjectRegistrar_##ClassName(#ClassName);
