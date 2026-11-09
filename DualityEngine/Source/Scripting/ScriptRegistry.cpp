#include "DualityEngine/Scripting/ScriptRegistry.h"

#include <unordered_map>

#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        struct RegistryEntry {
            std::string Name;
            ScriptLanguage Language = ScriptLanguage::Cpp;
            Behaviour* (*Create)() = nullptr;          // C++ (GameScripts) factory
            std::function<Behaviour*()> CreateDynamic; // per-class factory (TypeScript)
            void (*Destroy)(Behaviour*) = nullptr;
            std::vector<FieldHandle> Fields;
        };

        std::unordered_map<std::string, RegistryEntry>& Map() {
            static std::unordered_map<std::string, RegistryEntry> map;
            return map;
        }
    }

    void ScriptRegistry::Clear() {
        Map().clear();
    }

    void ScriptRegistry::ClearLanguage(ScriptLanguage language) {
        auto& map = Map();
        for (auto it = map.begin(); it != map.end();) {
            if (it->second.Language == language)
                it = map.erase(it);
            else
                ++it;
        }
    }

    void ScriptRegistry::Register(const ScriptFactoryEntry& entry) {
        if (!entry.Name)
            return;
        auto& map = Map();
        auto existing = map.find(entry.Name);
        if (existing != map.end() && existing->second.Language != ScriptLanguage::Cpp)
            Log::Warn(std::string("ScriptRegistry: C++ script '") + entry.Name + "' replaces the TypeScript class of the same name");
        RegistryEntry stored;
        stored.Name = entry.Name;
        stored.Language = ScriptLanguage::Cpp;
        stored.Create = entry.Create;
        stored.Destroy = entry.Destroy;
        stored.Fields = entry.Fields;
        map[stored.Name] = std::move(stored);
    }

    bool ScriptRegistry::RegisterDynamic(const std::string& name, ScriptLanguage language,
                                         std::function<Behaviour*()> create, void (*destroy)(Behaviour*),
                                         std::vector<FieldHandle> fields) {
        auto& map = Map();
        auto existing = map.find(name);
        if (existing != map.end() && existing->second.Language == ScriptLanguage::Cpp) {
            Log::Error("ScriptRegistry: script class '" + name + "' is already defined in C++ -- rename the TypeScript class");
            return false;
        }
        RegistryEntry stored;
        stored.Name = name;
        stored.Language = language;
        stored.CreateDynamic = std::move(create);
        stored.Destroy = destroy;
        stored.Fields = std::move(fields);
        map[name] = std::move(stored);
        return true;
    }

    bool ScriptRegistry::IsRegistered(const std::string& className) {
        const auto it = Map().find(className);
        return it != Map().end() && (it->second.Create || it->second.CreateDynamic) && it->second.Destroy;
    }

    bool ScriptRegistry::TryCreate(const std::string& className, Behaviour** outInstance, void (**outDestroy)(Behaviour*)) {
        if (!outInstance || !outDestroy || !IsRegistered(className))
            return false;
        auto it = Map().find(className);
        *outInstance = it->second.Create ? it->second.Create() : it->second.CreateDynamic();
        *outDestroy = it->second.Destroy;
        return *outInstance != nullptr;
    }

    int ScriptRegistry::Count() {
        return static_cast<int>(Map().size());
    }

    std::vector<std::string> ScriptRegistry::GetAllClassNames() {
        std::vector<std::string> names;
        names.reserve(Map().size());
        for (auto& [name, entry] : Map())
            names.push_back(name);
        return names;
    }

    ScriptLanguage ScriptRegistry::GetLanguage(const std::string& className) {
        auto it = Map().find(className);
        return it != Map().end() ? it->second.Language : ScriptLanguage::Cpp;
    }

    const std::vector<FieldHandle>& ScriptRegistry::GetFields(const std::string& className) {
        static const std::vector<FieldHandle> empty;
        auto it = Map().find(className);
        return it != Map().end() ? it->second.Fields : empty;
    }

}
