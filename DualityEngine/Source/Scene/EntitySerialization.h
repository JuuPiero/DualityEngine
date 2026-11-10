#pragma once

// Internal (not under Include/) -- the per-entity "walk every TypeRegistry-registered
// component, read/write each field via FieldValueToJson/JsonToFieldValue" logic, shared by
// SceneSerializer.cpp (a whole scene) and PrefabSerializer.cpp (one entity + its descendant
// subtree) so the two don't duplicate the same std::visit dispatch tables. Neither function
// touches HierarchyComponent's own Parent/Children -- that's bespoke per-caller indexing
// (a whole scene's entities vs. a prefab's own local subtree mean different index spaces),
// handled by SceneSerializer/PrefabSerializer themselves, same as before this was extracted.

#include <unordered_map>
#include <vector>

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

    // EntityRef values serialize as {"$entity": rawHandle}. Files store them as the index of
    // the referenced entity inside that file's own "Entities" array (-1 = none / outside the
    // file): `indexOf` maps the live handles that belong to the file. Shared by scenes, prefabs
    // and the editor clipboard so every format keeps references valid across save/load.
    void EncodeEntityReferenceIndices(json& value, const std::unordered_map<entt::entity, int>& indexOf);

    // Inverse of EncodeEntityReferenceIndices once every entity of the file exists: every
    // EntityRef (component fields, script overrides, nested structs, lists) currently holding a
    // file index is rewritten to the handle of `orderedEntities[index]`.
    void ResolveLoadedEntityReferences(Scene& scene, const std::vector<Entity>& orderedEntities);
    // Same, but only rewrites the references held by `entitiesToFix`; indices still resolve
    // through `indexTable` (which may contain entities outside the file, or empty Entities for
    // "missing" -- an empty Entity resolves to EntityRef::Invalid).
    void ResolveLoadedEntityReferences(Scene& scene, const std::vector<Entity>& entitiesToFix, const std::vector<Entity>& indexTable);

}
