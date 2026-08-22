#pragma once

#include <functional>
#include <string>
#include <variant>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Distinct wrapper so a glm::vec4 field can be tagged "this is a color"
    // (ColorEdit4 widget, no [0,1] clamp assumption elsewhere) without a
    // separate enum tag -- the value's own type in the FieldValue variant
    // *is* the dispatch key, for both the Properties panel and
    // serialization.
    struct Color4 { glm::vec4 Value; };

    using FieldValue = std::variant<int, float, bool, std::string, glm::vec2, glm::vec3, glm::vec4, Color4, Screen>;

    // A named, type-erased accessor for one field of a component/script
    // instance. Reflection is only ever walked from the Properties panel
    // and (de)serialization -- never from a per-frame hot path -- so the
    // std::function indirection here is a non-issue.
    struct FieldHandle {
        std::string Name;
        std::function<FieldValue(void*)> Get;
        std::function<void(void*, const FieldValue&)> Set;
    };

    template<typename C, typename T>
    FieldHandle MakeField(const std::string& name, T C::* member) {
        FieldHandle handle;
        handle.Name = name;
        handle.Get = [member](void* instance) -> FieldValue {
            return FieldValue(static_cast<C*>(instance)->*member);
        };
        handle.Set = [member](void* instance, const FieldValue& value) {
            static_cast<C*>(instance)->*member = std::get<T>(value);
        };
        return handle;
    }

    // Same as MakeField, but for a glm::vec4 member that should be treated
    // (and edited/serialized) as a color rather than a generic 4-vector.
    template<typename C>
    FieldHandle MakeColorField(const std::string& name, glm::vec4 C::* member) {
        FieldHandle handle;
        handle.Name = name;
        handle.Get = [member](void* instance) -> FieldValue {
            return FieldValue(Color4{ static_cast<C*>(instance)->*member });
        };
        handle.Set = [member](void* instance, const FieldValue& value) {
            static_cast<C*>(instance)->*member = std::get<Color4>(value).Value;
        };
        return handle;
    }

}
