#include "DualityEditor/EntityIdRegistry.h"

#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        uint32_t Key(Entity entity) { return static_cast<uint32_t>(entity.Handle()); }
    }

    uint64_t EntityIdRegistry::Get(Entity entity) {
        if (!entity)
            return 0;
        auto it = m_IdOfHandle.find(Key(entity));
        if (it != m_IdOfHandle.end())
            return it->second;
        const uint64_t id = m_NextId++;
        m_IdOfHandle[Key(entity)] = id;
        return id;
    }

    uint64_t EntityIdRegistry::Peek(Entity entity) const {
        if (!entity)
            return 0;
        auto it = m_IdOfHandle.find(Key(entity));
        return it == m_IdOfHandle.end() ? 0 : it->second;
    }

    Entity EntityIdRegistry::Find(Scene& scene, uint64_t id) const {
        if (id == 0)
            return {};
        // Linear in the number of known handles; only called for small sets (selection,
        // clipboard references, test scripts), never per entity per frame.
        for (const auto& [handle, value] : m_IdOfHandle) {
            if (value != id)
                continue;
            const entt::entity entity = static_cast<entt::entity>(handle);
            if (scene.Registry().valid(entity))
                return Entity(entity, &scene);
        }
        return {};
    }

    std::vector<uint64_t> EntityIdRegistry::CaptureOrder(Scene& scene) {
        std::vector<uint64_t> ids;
        const std::vector<Entity> order = scene.GetHierarchyTraversalOrder();
        ids.reserve(order.size());
        for (Entity entity : order)
            ids.push_back(Get(entity));
        return ids;
    }

    void EntityIdRegistry::ApplyOrder(Scene& scene, const std::vector<uint64_t>& ids) {
        // Every handle of the previous scene generation is gone; drop them so a recycled handle
        // can never inherit an old id.
        m_IdOfHandle.clear();
        const std::vector<Entity> order = scene.GetHierarchyTraversalOrder();
        for (size_t i = 0; i < order.size(); ++i) {
            if (i < ids.size() && ids[i] != 0) {
                m_IdOfHandle[Key(order[i])] = ids[i];
                if (ids[i] >= m_NextId)
                    m_NextId = ids[i] + 1;
            } else {
                m_IdOfHandle[Key(order[i])] = m_NextId++;
            }
        }
    }

    void EntityIdRegistry::Clear() {
        m_IdOfHandle.clear();
    }

    std::vector<uint64_t> EntityIdRegistry::IdsOf(const std::vector<Entity>& entities) {
        std::vector<uint64_t> ids;
        ids.reserve(entities.size());
        for (Entity entity : entities) {
            if (entity)
                ids.push_back(Get(entity));
        }
        return ids;
    }

    std::vector<Entity> EntityIdRegistry::Resolve(Scene& scene, const std::vector<uint64_t>& ids) const {
        std::vector<Entity> entities;
        for (uint64_t id : ids) {
            if (Entity entity = Find(scene, id))
                entities.push_back(entity);
        }
        return entities;
    }

}
