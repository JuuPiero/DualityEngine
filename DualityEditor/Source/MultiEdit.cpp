#include "DualityEditor/MultiEdit.h"

#include <cmath>

#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Reflection/TypeRegistry.h"

namespace Duality::MultiEdit {

    namespace {
        bool IsSmallNumericVector(const nlohmann::json& value) {
            return value.is_array() && !value.empty() && value.size() <= 4 && value[0].is_number();
        }
    }

    bool NearlyEqual(const nlohmann::json& a, const nlohmann::json& b) {
        if (a.is_number() && b.is_number())
            return std::abs(a.get<double>() - b.get<double>()) < 1e-4;
        if (a.is_array() && b.is_array()) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (!NearlyEqual(a[i], b[i]))
                    return false;
            return true;
        }
        return a == b;
    }

    uint32_t MixedMask(const FieldHandle& field, ComponentTypeInfo& type, const std::vector<Entity>& targets) {
        if (targets.size() < 2)
            return 0;
        const nlohmann::json primary = FieldValueToJson(field.Get(type.GetPtr(targets.front())));
        uint32_t mask = 0;
        for (size_t t = 1; t < targets.size(); ++t) {
            const nlohmann::json other = FieldValueToJson(field.Get(type.GetPtr(targets[t])));
            if (IsSmallNumericVector(primary) && other.is_array() && other.size() == primary.size()) {
                for (size_t i = 0; i < primary.size(); ++i)
                    if (!NearlyEqual(primary[i], other[i]))
                        mask |= 1u << i;
            } else if (!NearlyEqual(primary, other)) {
                mask |= 1u;
            }
        }
        return mask;
    }

    void Propagate(const FieldHandle& field, ComponentTypeInfo& type, const std::vector<Entity>& targets,
        const nlohmann::json& before, const nlohmann::json& after) {
        for (size_t t = 1; t < targets.size(); ++t) {
            void* component = type.GetPtr(targets[t]);
            const FieldValue current = field.Get(component);
            nlohmann::json value = FieldValueToJson(current);
            if (IsSmallNumericVector(after) && before.is_array() && before.size() == after.size() &&
                value.is_array() && value.size() == after.size()) {
                for (size_t i = 0; i < after.size(); ++i)
                    if (!NearlyEqual(before[i], after[i]))
                        value[i] = after[i];
            } else {
                value = after;
            }
            field.Set(component, JsonToFieldValue(value, current));
        }
    }

}
