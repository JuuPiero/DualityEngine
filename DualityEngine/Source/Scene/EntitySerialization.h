#pragma once

// Internal (not under Include/) -- the per-entity "walk every TypeRegistry-registered
// component, read/write each field via FieldValueToJson/JsonToFieldValue" logic, shared by
// SceneSerializer.cpp (a whole scene) and PrefabSerializer.cpp (one entity + its descendant
// subtree) so the two don't duplicate the same std::visit dispatch tables. Neither function
// touches HierarchyComponent's own Parent/Children -- that's bespoke per-caller indexing
// (a whole scene's entities vs. a prefab's own local subtree mean different index spaces),
// handled by SceneSerializer/PrefabSerializer themselves, same as before this was extracted.

#include <nlohmann/json.hpp>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    using json = nlohmann::json;

    // Writes every field of every TypeRegistry-registered component present on `entity`
    // into `outEntityJson`, keyed by component DisplayName -> {fieldName: value}.
    void SerializeEntityComponents(Entity entity, json& outEntityJson);

    // Inverse of SerializeEntityComponents -- for every key in `entityJson` that resolves
    // to a registered component type (via TypeRegistry::Find), adds it to `entity`
    // (TypeRegistry::AddDefault) and hydrates whichever fields are present in the JSON.
    // Skips the "Parent" key (the caller's own bespoke hierarchy field) and logs a warning
    // for any other unrecognized key, matching SceneSerializer::Deserialize's original
    // behavior exactly.
    void DeserializeEntityComponents(Entity entity, const json& entityJson);

}
