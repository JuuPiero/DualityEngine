#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Saves/instantiates one entity + its full descendant subtree as a reusable JSON
    // asset (a ".prefab" file, same convention as Material's ".mat") --
    // Unity's Prefab. Shares SceneSerializer's own per-entity component
    // serialize/deserialize logic (see EntitySerialization.h) rather than duplicating it,
    // just scoped to one subtree instead of the whole scene.
    //
    class PrefabSerializer {
    public:
        // Collects `root` and every descendant (via HierarchyComponent::Children,
        // depth-first) and writes them to `path`. `root`'s own real parent in the live
        // scene is NOT recorded -- a prefab has no scene context of its own; Instantiate
        // re-parents the loaded subtree's root wherever it's dropped instead.
        static bool Save(Entity root, const std::string& path);

        // Loads the prefab at `path` into `scene` as a brand new copy of its saved
        // subtree, then attaches the subtree's root under `parent` (Entity{} = scene
        // root) via Scene::SetParent. Returns the new root Entity, or an empty Entity if
        // the file couldn't be loaded/parsed.
        static Entity Instantiate(Scene& scene, const std::string& path, Entity parent = {},
            const AssetRef& sourcePrefab = {});

        // Writes an instance's current subtree back to its source prefab, recreates an
        // instance from that source while retaining its parent/sibling position, or breaks
        // the connection.  Apply/Revert intentionally operate on the selected instance root.
        static bool Apply(Entity instanceRoot);
        static Entity Revert(Scene& scene, Entity instanceRoot);
        static bool Unpack(Entity instanceRoot);

        // Editor/runtime-safe in-memory duplicate of `source` and every descendant. The copy is
        // inserted immediately after source in the same sibling list, keeps authored component
        // values/scripts, and remaps EntityRef values that pointed inside the copied subtree to
        // their corresponding copied entities. Returns the copied root or an empty Entity for
        // an invalid source.
        static Entity Duplicate(Scene& scene, Entity source);

        // Editor clipboard (Copy / Paste). CopyToJson serializes each root with its subtree
        // (roots nested inside another copied root are folded into it). EntityRefs between
        // copied entities stay connected to the pasted copies; references to entities outside
        // the copy are kept as external handles and reconnected on paste when the target still
        // exists in the destination scene (`externalRemap` translates handles of the copy-time
        // scene into the paste-time scene, e.g. after an Undo rebuilt every handle). Pasted roots
        // keep their copied world transform under `parent` (empty = scene root).
        static nlohmann::json CopyToJson(Scene& scene, const std::vector<Entity>& roots);
        static std::vector<Entity> PasteFromJson(Scene& scene, const nlohmann::json& clipboard, Entity parent = {},
            const std::unordered_map<uint32_t, uint32_t>* externalRemap = nullptr);
    };

}
