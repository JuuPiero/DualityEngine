#include "DualityEngine/Scripting/ScriptRegistry.h"

#include <unordered_map>

namespace Duality {

    static std::unordered_map<std::string, ScriptFactoryEntry>& Map() {
        static std::unordered_map<std::string, ScriptFactoryEntry> map;
        return map;
    }

    void ScriptRegistry::Clear() {
        Map().clear();
    }

    void ScriptRegistry::Register(const ScriptFactoryEntry& entry) {
        Map()[entry.Name] = entry;
    }

    bool ScriptRegistry::TryCreate(const std::string& className, Behaviour** outInstance, void (**outDestroy)(Behaviour*)) {
        auto it = Map().find(className);
        if (it == Map().end())
            return false;
        *outInstance = it->second.Create();
        *outDestroy = it->second.Destroy;
        return true;
    }

    int ScriptRegistry::Count() {
        return static_cast<int>(Map().size());
    }

    const std::vector<FieldHandle>& ScriptRegistry::GetFields(const std::string& className) {
        static const std::vector<FieldHandle> empty;
        auto it = Map().find(className);
        return it != Map().end() ? it->second.Fields : empty;
    }

}
