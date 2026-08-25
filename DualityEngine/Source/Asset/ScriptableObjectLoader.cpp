#include "DualityEngine/Asset/ScriptableObjectLoader.h"

#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

using json = nlohmann::json;

namespace Duality {

    namespace {
        struct CacheEntry {
            std::string ClassName;
            ScriptableObject* Instance = nullptr;
            void (*Destroy)(ScriptableObject*) = nullptr;
        };

        std::unordered_map<std::string, CacheEntry> s_Cache;
    }

    ScriptableObjectLoader::Loaded ScriptableObjectLoader::Load(const std::string& path) {
        auto it = s_Cache.find(path);
        if (it != s_Cache.end())
            return { it->second.ClassName, it->second.Instance };

        std::ifstream file(path);
        if (!file) {
            Log::Warn("ScriptableObjectLoader: could not open '" + path + "'");
            return {};
        }

        json root;
        try {
            file >> root;
        } catch (const json::parse_error&) {
            Log::Warn("ScriptableObjectLoader: failed to parse '" + path + "'");
            return {};
        }

        std::string className = root.value("Class", std::string());
        const ScriptableObjectFactoryEntry* type = ScriptableObjectRegistry::Find(className);
        if (!type) {
            Log::Warn("ScriptableObjectLoader: unknown ScriptableObject class '" + className + "' (is GameScripts loaded?)");
            return { className, nullptr };
        }

        ScriptableObject* instance = type->Create();
        if (root.contains("Fields")) {
            const json& fieldsJson = root["Fields"];
            for (auto& field : type->Fields) {
                if (fieldsJson.contains(field.Name)) {
                    FieldValue current = field.Get(instance);
                    field.Set(instance, JsonToFieldValue(fieldsJson[field.Name], current));
                }
            }
        }

        s_Cache[path] = CacheEntry{ className, instance, type->Destroy };
        return { className, instance };
    }

    bool ScriptableObjectLoader::Save(const std::string& path, const std::string& className, ScriptableObject* instance) {
        const ScriptableObjectFactoryEntry* type = ScriptableObjectRegistry::Find(className);
        if (!type || !instance)
            return false;

        json root;
        root["Class"] = className;
        json fieldsJson;
        for (auto& field : type->Fields)
            fieldsJson[field.Name] = FieldValueToJson(field.Get(instance));
        root["Fields"] = fieldsJson;

        std::ofstream file(path);
        if (!file)
            return false;
        file << root.dump(4);
        return true;
    }

    ScriptableObjectLoader::Loaded ScriptableObjectLoader::Create(const std::string& path, const std::string& className) {
        const ScriptableObjectFactoryEntry* type = ScriptableObjectRegistry::Find(className);
        if (!type)
            return { className, nullptr };

        ScriptableObject* instance = type->Create();
        Save(path, className, instance);
        s_Cache[path] = CacheEntry{ className, instance, type->Destroy };
        return { className, instance };
    }

    void ScriptableObjectLoader::UnloadAll() {
        for (auto& [path, entry] : s_Cache) {
            if (entry.Instance && entry.Destroy)
                entry.Destroy(entry.Instance);
        }
        s_Cache.clear();
    }

}
