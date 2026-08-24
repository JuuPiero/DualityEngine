#pragma once

#include <string>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Saves/instantiates one entity + its full descendant subtree as a reusable JSON
    // asset (a ".prefab.json" file, same convention as Material's ".material.json") --
    // Unity's Prefab. Shares SceneSerializer's own per-entity component
    // serialize/deserialize logic (see EntitySerialization.h) rather than duplicating it,
    // just scoped to one subtree instead of the whole scene.
    //
    // Scope cut: no prefab-instance link -- an instantiated copy is a fully independent
    // set of entities afterward, with no "apply changes back to the prefab" support.
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
        static Entity Instantiate(Scene& scene, const std::string& path, Entity parent = {});
    };

}
