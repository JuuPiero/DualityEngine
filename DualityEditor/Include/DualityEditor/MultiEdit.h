#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    struct ComponentTypeInfo;
    struct FieldHandle;

    // Multi-object editing helpers (Unity: select several objects, edit a field once).
    // Kept free of ImGui so the Inspector's rules are unit-tested (Tests/EditorLogicTests.cpp).
    namespace MultiEdit {

        // Float-tolerant JSON comparison (numbers and nested arrays).
        bool NearlyEqual(const nlohmann::json& a, const nlohmann::json& b);

        // Which parts of `field` differ between targets[0] and the other targets: bit i = vector
        // element i (numeric vectors up to 4 wide), bit 0 = the whole value otherwise.
        uint32_t MixedMask(const FieldHandle& field, ComponentTypeInfo& type, const std::vector<Entity>& targets);

        // targets[0]'s field changed from `before` to `after` (FieldValueToJson); applies the same
        // edit to targets[1..]. For numeric vectors only the edited elements are copied, so
        // editing X of several objects keeps each object's own Y/Z.
        void Propagate(const FieldHandle& field, ComponentTypeInfo& type, const std::vector<Entity>& targets,
            const nlohmann::json& before, const nlohmann::json& after);
    }

}
