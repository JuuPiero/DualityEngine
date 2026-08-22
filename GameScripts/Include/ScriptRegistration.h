#pragma once

#include <vector>

#include "DualityEngine/Scripting/ScriptModule.h"

// Self-registration machinery living entirely inside this module's own
// binary (DLL today, statically linked into the device build in a later
// phase) -- it never touches DualityEngine's compiled code, only its
// headers, so it stays a single, un-duplicated copy regardless of how this
// module ends up linked. See ScriptModuleExports.cpp for how the resulting
// list crosses back out to the host.
namespace Duality {

    inline std::vector<ScriptFactoryEntry>& GetLocalScriptFactories() {
        static std::vector<ScriptFactoryEntry> factories;
        return factories;
    }

    template<typename T>
    struct ScriptRegistrar {
        explicit ScriptRegistrar(const char* name) {
            GetLocalScriptFactories().push_back(ScriptFactoryEntry{
                name,
                []() -> Behaviour* { return new T(); },
                [](Behaviour* instance) { delete instance; },
            });
        }
    };

}

#define REGISTER_BEHAVIOUR(ClassName) \
    static Duality::ScriptRegistrar<ClassName> s_ScriptRegistrar_##ClassName(#ClassName);
