#include "DualityEngine/Scene/SceneSerializer.h"

#include <fstream>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "EntitySerialization.h"

using json = nlohmann::json;

namespace Duality {

    bool SceneSerializer::Serialize(const std::string& path) {
        json root;
        json entities = json::array();

        // entt::entity handles don't survive save/load (Deserialize below creates
        // fresh ones), so HierarchyComponent::Parent -- a live Entity -- can't be
        // written directly. Instead, every entity's position in this same array is
        // used as its stable id for this one file: `indexOf` resolves a parent
        // Entity to that index, written as a plain "Parent" integer below. This is
        // bespoke (not routed through SerializeEntityComponents/TypeRegistry), matching
        // how HierarchyComponent itself is never TypeRegistry::Register'd.
        std::vector<Entity> orderedEntities;
        for (auto handle : m_Scene.Registry().view<NameComponent>())
            orderedEntities.emplace_back(handle, &m_Scene);

        std::unordered_map<entt::entity, int> indexOf;
        for (size_t i = 0; i < orderedEntities.size(); i++)
            indexOf[orderedEntities[i].Handle()] = static_cast<int>(i);

        for (Entity entity : orderedEntities) {
            json entityJson;
            SerializeEntityComponents(entity, entityJson);

            Entity parent = entity.GetComponent<HierarchyComponent>().Parent;
            entityJson["Parent"] = parent ? indexOf[parent.Handle()] : -1;

            entities.push_back(entityJson);
        }

        root["Entities"] = entities;

        std::ofstream file(path);
        if (!file.is_open()) {
            Log::Error("SceneSerializer: could not open '" + path + "' for writing");
            return false;
        }
        file << root.dump(2);
        Log::Info("Scene saved to '" + path + "'");
        return true;
    }

    bool SceneSerializer::Deserialize(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            Log::Error("SceneSerializer: could not open '" + path + "' for reading");
            return false;
        }

        json root;
        try {
            file >> root;
        } catch (const json::parse_error& e) {
            Log::Error("SceneSerializer: failed to parse '" + path + "': " + e.what());
            return false;
        }

        if (!root.contains("Entities"))
            return false;

        // First pass: create every entity and hydrate its registered components, in
        // file order -- `orderedEntities` ends up index-aligned with root["Entities"]
        // itself, which the second pass below relies on to resolve "Parent" indices
        // (entt::entity handles are freshly assigned by CreateEntity, so they can't
        // be known ahead of time; this is exactly why Serialize wrote indices
        // instead of handles).
        std::vector<Entity> orderedEntities;
        for (auto& entityJson : root["Entities"]) {
            Entity entity = m_Scene.CreateEntity();
            DeserializeEntityComponents(entity, entityJson);
            orderedEntities.push_back(entity);
        }

        // Second pass: every entity now exists, so "Parent" indices can be resolved.
        // preserveWorldPosition=false since the Transform just loaded above is
        // already the correct LOCAL value -- SetParent's default (true) is only for
        // interactive drag-drop reparenting, where re-deriving local from a captured
        // world transform is exactly what's wanted.
        for (size_t i = 0; i < orderedEntities.size(); i++) {
            int parentIndex = root["Entities"][i].value("Parent", -1);
            if (parentIndex >= 0 && parentIndex < static_cast<int>(orderedEntities.size()))
                m_Scene.SetParent(orderedEntities[i], orderedEntities[parentIndex], {}, /*preserveWorldPosition=*/false);
        }

        Log::Info("Scene loaded from '" + path + "'");
        return true;
    }

}
