#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

namespace Duality {

    static std::vector<ScriptableObjectFactoryEntry>& List() {
        static std::vector<ScriptableObjectFactoryEntry> list;
        return list;
    }

    void ScriptableObjectRegistry::Clear() {
        List().clear();
    }

    void ScriptableObjectRegistry::Register(const ScriptableObjectFactoryEntry& entry) {
        for (auto& existing : List()) {
            if (existing.Name == std::string(entry.Name)) {
                existing = entry;
                return;
            }
        }
        List().push_back(entry);
    }

    const ScriptableObjectFactoryEntry* ScriptableObjectRegistry::Find(const std::string& className) {
        for (auto& entry : List()) {
            if (entry.Name == className)
                return &entry;
        }
        return nullptr;
    }

    const std::vector<ScriptableObjectFactoryEntry>& ScriptableObjectRegistry::All() {
        return List();
    }

}
