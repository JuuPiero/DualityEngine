#include "DualityEngine/Reflection/FieldSerialization.h"

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
            } else if constexpr (std::is_same_v<T, BodyType>) {
                const char* names[] = { "Static", "Kinematic", "Dynamic" };
                return names[static_cast<int>(v)];
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
            } else if constexpr (std::is_same_v<T, BodyType>) {
                std::string name = j.get<std::string>();
                if (name == "Static") return BodyType::Static;
                if (name == "Kinematic") return BodyType::Kinematic;
                return BodyType::Dynamic;
            } else if constexpr (std::is_same_v<T, EntityRef>) {
                // Never round-tripped -- see EntityRef's own comment in Field.h.
                return EntityRef{};
            } else {
                return j.get<T>(); // int, float, bool, std::string
            }
        }, prototype);
    }

}
