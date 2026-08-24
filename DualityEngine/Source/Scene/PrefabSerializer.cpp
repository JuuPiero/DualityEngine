#include "DualityEngine/Scene/PrefabSerializer.h"

#include <fstream>
#include <functional>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "EntitySerialization.h"

using json = nlohmann::json;

namespace Duality {

    bool PrefabSerializer::Save(Entity root, const std::string& path) {
        // Depth-first collect of root + every descendant -- same recursive-children-walk
        // shape as Scene::DestroyEntity's own subtree traversal, just building a list
        // instead of destroying. Pre-order (a parent is always visited, and so assigned
        // its index, before any of its children) so every child's "Parent" index below is
        // guaranteed to already exist in indexOf by the time it's looked up.
        std::vector<Entity> orderedEntities;
        std::unordered_map<entt::entity, int> indexOf;
        std::function<void(Entity)> collect = [&](Entity entity) {
            indexOf[entity.Handle()] = static_cast<int>(orderedEntities.size());
            orderedEntities.push_back(entity);
            for (Entity child : entity.GetComponent<HierarchyComponent>().Children)
                collect(child);
        };
        collect(root);

        json entities = json::array();
        for (Entity entity : orderedEntities) {
            json entityJson;
            SerializeEntityComponents(entity, entityJson);

            // root's real parent in the live scene is deliberately not recorded -- a
            // prefab has no scene context of its own (see Instantiate). Every other
            // entity in the subtree is guaranteed to have its own parent already indexed,
            // since collect() above always visits a parent before its children.
            if (entity.Handle() == root.Handle())
                entityJson["Parent"] = -1;
            else
                entityJson["Parent"] = indexOf[entity.GetComponent<HierarchyComponent>().Parent.Handle()];

            entities.push_back(entityJson);
        }

        json rootJson;
        rootJson["Entities"] = entities;

        std::ofstream file(path);
        if (!file.is_open()) {
            Log::Error("PrefabSerializer: could not open '" + path + "' for writing");
            return false;
        }
        file << rootJson.dump(2);
        Log::Info("Prefab saved to '" + path + "'");
        return true;
    }

    Entity PrefabSerializer::Instantiate(Scene& scene, const std::string& path, Entity parent) {
        std::ifstream file(path);
        if (!file.is_open()) {
            Log::Error("PrefabSerializer: could not open '" + path + "' for reading");
            return Entity{};
        }

        json root;
        try {
            file >> root;
        } catch (const json::parse_error& e) {
            Log::Error("PrefabSerializer: failed to parse '" + path + "': " + e.what());
            return Entity{};
        }

        if (!root.contains("Entities") || root["Entities"].empty()) {
            Log::Error("PrefabSerializer: '" + path + "' has no entities");
            return Entity{};
        }

        // Same two-pass shape as SceneSerializer::Deserialize -- create every entity in
        // file order first (index-aligned with root["Entities"]), then resolve "Parent"
        // indices once every entity actually exists.
        std::vector<Entity> orderedEntities;
        for (auto& entityJson : root["Entities"]) {
            Entity entity = scene.CreateEntity();
            DeserializeEntityComponents(entity, entityJson);
            orderedEntities.push_back(entity);
        }

        for (size_t i = 0; i < orderedEntities.size(); i++) {
            int parentIndex = root["Entities"][i].value("Parent", -1);
            if (parentIndex >= 0 && parentIndex < static_cast<int>(orderedEntities.size()))
                scene.SetParent(orderedEntities[i], orderedEntities[parentIndex], {}, /*preserveWorldPosition=*/false);
        }

        // Index 0 is always the prefab's own root -- Save's collect() visits it first,
        // before any descendant.
        Entity subtreeRoot = orderedEntities[0];
        scene.SetParent(subtreeRoot, parent);

        Log::Info("Prefab instantiated from '" + path + "'");
        return subtreeRoot;
    }

}
