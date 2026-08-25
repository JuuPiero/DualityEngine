#include "DualityEngine/Scripting/ScriptModule.h"
#include "ScriptRegistration.h"
#include "ScriptableObjectRegistration.h"

#if defined(_WIN32)
#define DE_SCRIPT_EXPORT extern "C" __declspec(dllexport)
#else
#define DE_SCRIPT_EXPORT extern "C"
#endif

// The one function the host (desktop Editor's DLL loader today; a
// statically-linked device build in a later phase) calls to retrieve every
// REGISTER_BEHAVIOUR'd class in this module.
DE_SCRIPT_EXPORT void GetScriptFactories(const Duality::ScriptFactoryEntry** outEntries, int* outCount) {
    auto& factories = Duality::GetLocalScriptFactories();
    *outEntries = factories.data();
    *outCount = static_cast<int>(factories.size());
}

// Same shape, for every REGISTER_SCRIPTABLE_OBJECT'd class in this module.
DE_SCRIPT_EXPORT void GetScriptableObjectFactories(const Duality::ScriptableObjectFactoryEntry** outEntries, int* outCount) {
    auto& factories = Duality::GetLocalScriptableObjectFactories();
    *outEntries = factories.data();
    *outCount = static_cast<int>(factories.size());
}
