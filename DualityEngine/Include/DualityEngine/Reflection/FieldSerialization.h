#pragma once

#include <nlohmann/json.hpp>

#include "DualityEngine/Reflection/Field.h"

// Generic FieldValue <-> JSON conversion, shared by every consumer of the reflection system
// that needs to persist a field to disk: EntitySerialization.cpp (components on an Entity),
// PrefabSerializer.cpp (through EntitySerialization), and ScriptableObjectLoader.cpp (fields on
// a ScriptableObject instance, which isn't Entity-coupled at all). Extracted here rather than
// left local to EntitySerialization.cpp once a second, non-Entity consumer needed the exact
// same std::visit dispatch tables -- same reasoning as EntitySerialization.cpp itself being
// extracted out of SceneSerializer.cpp for Prefab's sake.

namespace Duality {

    using json = nlohmann::json;

    // Converts a FieldValue to JSON generically -- adding a new component/ScriptableObject
    // field never touches this function, only the FieldValue variant's alternative types matter.
    json FieldValueToJson(const FieldValue& value);

    // Parses `j` into whichever FieldValue alternative `prototype` currently holds -- the
    // existing (default-constructed) value tells us which shape to expect, so the JSON itself
    // doesn't need an explicit type tag.
    FieldValue JsonToFieldValue(const json& j, const FieldValue& prototype);

}
