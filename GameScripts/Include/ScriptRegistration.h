#pragma once

#include <type_traits>
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

    namespace Detail {
        // Detects whether T declares `static std::vector<FieldHandle> Fields()` (via
        // DUALITY_PROPERTIES, see Reflection/PropertyMacros.h) -- unlike ScriptableObject,
        // a Behaviour is NOT required to declare one: existing scripts written before this
        // feature existed have none, and must keep compiling with zero Inspector fields.
        template<typename T, typename = void>
        struct HasFieldsMethod : std::false_type {};
        template<typename T>
        struct HasFieldsMethod<T, std::void_t<decltype(T::Fields())>> : std::true_type {};

        template<typename T>
        std::vector<FieldHandle> GetFieldsIfDeclared() {
            if constexpr (HasFieldsMethod<T>::value)
                return T::Fields();
            else
                return {};
        }
    }

    template<typename T>
    struct ScriptRegistrar {
        explicit ScriptRegistrar(const char* name) {
            GetLocalScriptFactories().push_back(ScriptFactoryEntry{
                name,
                []() -> Behaviour* { return new T(); },
                [](Behaviour* instance) { delete instance; },
                Detail::GetFieldsIfDeclared<T>(),
            });
        }
    };

}

#define REGISTER_BEHAVIOUR(ClassName) \
    static Duality::ScriptRegistrar<ClassName> s_ScriptRegistrar_##ClassName(#ClassName);
