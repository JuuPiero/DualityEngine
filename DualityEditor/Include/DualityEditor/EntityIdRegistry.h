#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    class Scene;

    // Editor-only stable identity for scene entities (Unity's per-object instance/file id).
    //
    // EnTT handles are not stable across the editor's snapshot operations: Undo/Redo, Stop (Play
    // revert) and Prefab Mode all rebuild the Scene from JSON, which allocates fresh handles. A
    // stable id lets selection, Hierarchy foldout state, clipboard references and history
    // restore survive those rebuilds. Ids are runtime-only: they are never written into a
    // .scene/.prefab file (the on-disk format is unchanged), only into the editor's in-memory
    // snapshots next to the scene JSON (see SceneHistory).
    //
    // Scene snapshots serialize entities in Scene::GetHierarchyTraversalOrder() and deserialize
    // them back in that same order, so "the id of the n-th entity in traversal order" is enough
    // to re-attach ids after a rebuild (CaptureOrder / ApplyOrder).
    class EntityIdRegistry {
    public:
        // Returns the entity's id, assigning a fresh one the first time it is seen.
        uint64_t Get(Entity entity);
        // Returns 0 if the entity has no id yet (without assigning one).
        uint64_t Peek(Entity entity) const;
        // Finds the live entity carrying `id` in `scene` (empty Entity if none).
        Entity Find(Scene& scene, uint64_t id) const;

        // Ids of every entity in hierarchy traversal order (assigning missing ones).
        std::vector<uint64_t> CaptureOrder(Scene& scene);
        // Re-attaches ids captured by CaptureOrder after the scene was rebuilt from the snapshot
        // taken at the same moment. Extra entities (if any) get fresh ids.
        void ApplyOrder(Scene& scene, const std::vector<uint64_t>& ids);

        // Forget every mapping (scene switch / project switch).
        void Clear();

        std::vector<uint64_t> IdsOf(const std::vector<Entity>& entities);
        std::vector<Entity> Resolve(Scene& scene, const std::vector<uint64_t>& ids) const;

    private:
        std::unordered_map<uint32_t, uint64_t> m_IdOfHandle;
        uint64_t m_NextId = 1;
    };

}
