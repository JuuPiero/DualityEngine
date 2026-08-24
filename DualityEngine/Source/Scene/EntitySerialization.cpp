#include "EntitySerialization.h"

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"

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
            } else if constexpr (std::is_same_v<T, ProjectionType>) {
                return v == ProjectionType::Orthographic ? "Orthographic" : "Perspective";
            } else if constexpr (std::is_same_v<T, MeshPrimitive>) {
                const char* names[] = { "Cube", "Sphere", "Plane" };
                return names[static_cast<int>(v)];
            } else if constexpr (std::is_same_v<T, UIAnchor>) {
                const char* names[] = { "TopLeft", "TopCenter", "TopRight", "MiddleLeft", "MiddleCenter", "MiddleRight", "BottomLeft", "BottomCenter", "BottomRight" };
                return names[static_cast<int>(v)];
            } else if constexpr (std::is_same_v<T, AssetRef>) {
                return v.Guid;
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
            } else if constexpr (std::is_same_v<T, ProjectionType>) {
                return j.get<std::string>() == "Orthographic" ? ProjectionType::Orthographic : ProjectionType::Perspective;
            } else if constexpr (std::is_same_v<T, MeshPrimitive>) {
                std::string name = j.get<std::string>();
                if (name == "Sphere") return MeshPrimitive::Sphere;
                if (name == "Plane") return MeshPrimitive::Plane;
                return MeshPrimitive::Cube;
            } else if constexpr (std::is_same_v<T, UIAnchor>) {
                std::string name = j.get<std::string>();
                const char* names[] = { "TopLeft", "TopCenter", "TopRight", "MiddleLeft", "MiddleCenter", "MiddleRight", "BottomLeft", "BottomCenter", "BottomRight" };
                for (int i = 0; i < 9; i++)
                    if (name == names[i]) return static_cast<UIAnchor>(i);
                return UIAnchor::TopLeft;
            } else if constexpr (std::is_same_v<T, AssetRef>) {
                return AssetRef{ j.get<std::string>() };
            } else {
                return j.get<T>(); // int, float, bool, std::string
            }
        }, prototype);
    }

    void SerializeEntityComponents(Entity entity, json& outEntityJson) {
        for (auto& type : TypeRegistry::All()) {
            if (!type.Has(entity))
                continue;
            void* component = type.GetPtr(entity);
            json fieldsJson;
            for (auto& field : type.Fields)
                fieldsJson[field.Name] = FieldValueToJson(field.Get(component));
            outEntityJson[type.DisplayName] = fieldsJson;
        }
    }

    void DeserializeEntityComponents(Entity entity, const json& entityJson) {
        for (auto& [typeName, fieldsJson] : entityJson.items()) {
            if (typeName == "Parent")
                continue; // bespoke hierarchy field, handled by the caller

            auto* type = TypeRegistry::Find(typeName);
            if (!type) {
                Log::Warn("EntitySerialization: unknown component type '" + typeName + "', skipping");
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

}
