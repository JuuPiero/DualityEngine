#include "DualityEngine/Scene/SceneSerializer.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"

using json = nlohmann::json;

namespace Duality {

    // Converts a FieldValue to JSON generically -- adding a new component or
    // field never touches this function, only the FieldValue variant's
    // alternative types matter.
    static json FieldValueToJson(const FieldValue& value) {
        return std::visit([](auto&& v) -> json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, glm::vec2>) {
                return json::array({ v.x, v.y });
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                return json::array({ v.x, v.y, v.z });
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                return json::array({ v.x, v.y, v.z, v.w });
            } else if constexpr (std::is_same_v<T, Color4>) {
                return json::array({ v.Value.x, v.Value.y, v.Value.z, v.Value.w });
            } else if constexpr (std::is_same_v<T, Screen>) {
                return v == Screen::Top ? "Top" : "Bottom";
            } else {
                return v; // int, float, bool, std::string
            }
        }, value);
    }

    // Parses `j` into whichever FieldValue alternative `prototype` currently
    // holds -- the existing (default-constructed) value tells us which
    // shape to expect, so the JSON itself doesn't need an explicit type tag.
    static FieldValue JsonToFieldValue(const json& j, const FieldValue& prototype) {
        return std::visit([&](auto&& proto) -> FieldValue {
            using T = std::decay_t<decltype(proto)>;
            if constexpr (std::is_same_v<T, glm::vec2>) {
                return glm::vec2{ j[0].get<float>(), j[1].get<float>() };
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                return glm::vec3{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                return glm::vec4{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
            } else if constexpr (std::is_same_v<T, Color4>) {
                return Color4{ glm::vec4{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() } };
            } else if constexpr (std::is_same_v<T, Screen>) {
                return j.get<std::string>() == "Top" ? Screen::Top : Screen::Bottom;
            } else {
                return j.get<T>(); // int, float, bool, std::string
            }
        }, prototype);
    }

    bool SceneSerializer::Serialize(const std::string& path) {
        json root;
        json entities = json::array();

        for (auto handle : m_Scene.Registry().view<TagComponent>()) {
            Entity entity(handle, &m_Scene);
            json entityJson;

            for (auto& type : TypeRegistry::All()) {
                if (!type.Has(entity))
                    continue;
                void* component = type.GetPtr(entity);
                json fieldsJson;
                for (auto& field : type.Fields)
                    fieldsJson[field.Name] = FieldValueToJson(field.Get(component));
                entityJson[type.DisplayName] = fieldsJson;
            }

            entities.push_back(entityJson);
        }

        root["Entities"] = entities;

        std::ofstream file(path);
        if (!file.is_open()) {
            Log::Error("SceneSerializer: could not open '" + path + "' for writing");
            return false;
        }
        file << root.dump(2);
        Log::Info("Scene saved to '" + path + "'");
        return true;
    }

    bool SceneSerializer::Deserialize(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            Log::Error("SceneSerializer: could not open '" + path + "' for reading");
            return false;
        }

        json root;
        try {
            file >> root;
        } catch (const json::parse_error& e) {
            Log::Error("SceneSerializer: failed to parse '" + path + "': " + e.what());
            return false;
        }

        if (!root.contains("Entities"))
            return false;

        for (auto& entityJson : root["Entities"]) {
            Entity entity = m_Scene.CreateEntity();

            for (auto& [typeName, fieldsJson] : entityJson.items()) {
                auto* type = TypeRegistry::Find(typeName);
                if (!type) {
                    Log::Warn("SceneSerializer: unknown component type '" + typeName + "', skipping");
                    continue;
                }

                type->AddDefault(entity);
                void* component = type->GetPtr(entity);
                for (auto& field : type->Fields) {
                    if (fieldsJson.contains(field.Name)) {
                        FieldValue current = field.Get(component);
                        field.Set(component, JsonToFieldValue(fieldsJson[field.Name], current));
                    }
                }
            }
        }

        Log::Info("Scene loaded from '" + path + "'");
        return true;
    }

}
