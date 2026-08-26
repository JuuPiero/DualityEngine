#include "DualityEngine/Reflection/FieldSerialization.h"

#include <any>

namespace Duality {

    json FieldValueToJson(const FieldValue& value) {
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
            } else if constexpr (std::is_same_v<T, std::vector<AssetRef>>) {
                json array = json::array();
                for (const AssetRef& item : v)
                    array.push_back(item.Guid);
                return array;
            } else if constexpr (std::is_same_v<T, BodyType>) {
                const char* names[] = { "Static", "Kinematic", "Dynamic" };
                return names[static_cast<int>(v)];
            } else if constexpr (std::is_same_v<T, NestedFieldValue>) {
                // Recurses into a nested JSON object keyed by the nested type's own field names
                // -- pure (no side effects), since NestedFieldValue's values live in its own
                // detached std::any-wrapped vector, not through a live instance pointer (see
                // NestedFieldValue's own comment in Field.h for why that matters).
                json obj = json::object();
                const auto& values = std::any_cast<const std::vector<FieldValue>&>(v.Values);
                auto fieldsFn = std::any_cast<std::vector<FieldHandle> (*)()>(v.FieldsFn);
                std::vector<FieldHandle> fields = fieldsFn();
                for (size_t i = 0; i < fields.size() && i < values.size(); i++)
                    obj[fields[i].Name] = FieldValueToJson(values[i]);
                return obj;
            } else if constexpr (std::is_same_v<T, EntityRef>) {
                // Never round-tripped -- see EntityRef's own comment in Field.h.
                return nullptr;
            } else {
                return v; // int, float, bool, std::string
            }
        }, value);
    }

    FieldValue JsonToFieldValue(const json& j, const FieldValue& prototype) {
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
            } else if constexpr (std::is_same_v<T, std::vector<AssetRef>>) {
                std::vector<AssetRef> result;
                for (const auto& guid : j)
                    result.push_back(AssetRef{ guid.get<std::string>() });
                return result;
            } else if constexpr (std::is_same_v<T, BodyType>) {
                std::string name = j.get<std::string>();
                if (name == "Static") return BodyType::Static;
                if (name == "Kinematic") return BodyType::Kinematic;
                return BodyType::Dynamic;
            } else if constexpr (std::is_same_v<T, NestedFieldValue>) {
                // Also pure -- `proto` (the nested struct's PREVIOUS values, from whatever
                // Get() call produced this prototype) supplies both the field list to recurse
                // over and, per-field, the prototype JsonToFieldValue's own recursive call
                // needs. A field missing from `j` (e.g. an older save predating that field)
                // keeps its prototype value rather than erroring, matching this whole function's
                // established graceful-degradation convention.
                auto fieldsFn = std::any_cast<std::vector<FieldHandle> (*)()>(proto.FieldsFn);
                std::vector<FieldHandle> fields = fieldsFn();
                const auto& protoValues = std::any_cast<const std::vector<FieldValue>&>(proto.Values);
                std::vector<FieldValue> values;
                for (size_t i = 0; i < fields.size() && i < protoValues.size(); i++)
                    values.push_back(j.contains(fields[i].Name) ? JsonToFieldValue(j.at(fields[i].Name), protoValues[i]) : protoValues[i]);
                return NestedFieldValue{ std::any(std::move(values)), proto.FieldsFn };
            } else if constexpr (std::is_same_v<T, EntityRef>) {
                // Never round-tripped -- see EntityRef's own comment in Field.h.
                return EntityRef{};
            } else {
                return j.get<T>(); // int, float, bool, std::string
            }
        }, prototype);
    }

}
