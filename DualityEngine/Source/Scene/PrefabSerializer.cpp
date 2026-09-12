#include "DualityEngine/Scene/PrefabSerializer.h"

#include <fstream>
#include <functional>
#include <initializer_list>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "EntitySerialization.h"

using json = nlohmann::json;

namespace Duality {

    namespace {
        // Prefabs were historically saved with the same pixel-space component values as
        // scenes. Keep an old prefab visually/physically equivalent when it is instantiated
        // into a version-2 world. New prefabs are explicitly stamped below.
        constexpr int WorldUnitsVersion = 2;
        constexpr int CanvasUIVersion = 1;
        constexpr float LegacyPixelsPerUnit = 100.0f;

        void ScaleNumber(json& value) {
            if (value.is_number())
                value = value.get<float>() / LegacyPixelsPerUnit;
        }

        void ScaleVector(json& value) {
            if (!value.is_array())
                return;
            for (json& item : value)
                ScaleNumber(item);
        }

        void ScaleVectors(json& component, std::initializer_list<const char*> names) {
            for (const char* name : names)
                if (component.contains(name))
                    ScaleVector(component[name]);
        }

        void ScaleScalars(json& component, std::initializer_list<const char*> names) {
            for (const char* name : names)
                if (component.contains(name))
                    ScaleNumber(component[name]);
        }

        void MigrateScriptOverrides(json& scripts) {
            if (!scripts.is_array())
                return;
            for (json& script : scripts) {
                if (!script.contains("PropertyOverrides") || script["PropertyOverrides"].is_null())
                    continue;
                json& overrides = script["PropertyOverrides"];
                const std::string className = script.value("ClassName", std::string());
                if (className == "PlayerController")
                    ScaleScalars(overrides, { "MoveSpeed", "JumpSpeed" });
                else if (className == "PatrolEnemyBehaviour")
                    ScaleScalars(overrides, { "Speed", "Range" });
                else if (className == "FeatureShowcaseBehaviour")
                    ScaleScalars(overrides, { "MoveSpeed" });
                else if (className == "BounceBehaviour")
                    ScaleScalars(overrides, { "Amplitude" });
            }
        }

        void MigrateLegacyPixelPrefab(json& root) {
            if (root.value("WorldUnitsVersion", 1) >= WorldUnitsVersion || !root.contains("Entities"))
                return;

            for (json& entity : root["Entities"]) {
                if (entity.contains("Transform"))
                    ScaleVectors(entity["Transform"], { "Translation" });
                if (entity.contains("Sprite Renderer"))
                    ScaleVectors(entity["Sprite Renderer"], { "Size" });
                if (entity.contains("Box Collider 2D"))
                    ScaleVectors(entity["Box Collider 2D"], { "Offset", "Size" });
                if (entity.contains("Circle Collider 2D")) {
                    ScaleVectors(entity["Circle Collider 2D"], { "Offset" });
                    ScaleScalars(entity["Circle Collider 2D"], { "Radius" });
                }
                if (entity.contains("Capsule Collider 2D")) {
                    ScaleVectors(entity["Capsule Collider 2D"], { "Offset" });
                    ScaleScalars(entity["Capsule Collider 2D"], { "Radius", "Height" });
                }
                if (entity.contains("Polygon Collider 2D"))
                    ScaleVectors(entity["Polygon Collider 2D"], { "Offset", "Vertex 0", "Vertex 1", "Vertex 2", "Vertex 3", "Vertex 4", "Vertex 5", "Vertex 6", "Vertex 7" });
                if (entity.contains("Box Collider 3D"))
                    ScaleVectors(entity["Box Collider 3D"], { "Offset", "Size" });
                if (entity.contains("Sphere Collider 3D")) {
                    ScaleVectors(entity["Sphere Collider 3D"], { "Offset" });
                    ScaleScalars(entity["Sphere Collider 3D"], { "Radius" });
                }
                if (entity.contains("Capsule Collider 3D")) {
                    ScaleVectors(entity["Capsule Collider 3D"], { "Offset" });
                    ScaleScalars(entity["Capsule Collider 3D"], { "Radius", "Height" });
                }
                if (entity.contains("Tilemap"))
                    ScaleVectors(entity["Tilemap"], { "Cell Size" });
                if (entity.contains("Line Renderer")) {
                    ScaleScalars(entity["Line Renderer"], { "Width" });
                    ScaleVectors(entity["Line Renderer"], { "Point 0", "Point 1", "Point 2", "Point 3", "Point 4", "Point 5", "Point 6", "Point 7" });
                }
                if (entity.contains("Follow Target"))
                    ScaleVectors(entity["Follow Target"], { "Offset" });
                if (entity.contains("Particle System")) {
                    ScaleScalars(entity["Particle System"], { "Start Speed", "Start Size" });
                    ScaleVectors(entity["Particle System"], { "Gravity", "Velocity Spread" });
                }
                if (entity.contains("Audio Source"))
                    ScaleScalars(entity["Audio Source"], { "Min Distance", "Max Distance" });
                if (entity.contains("Scripts"))
                    MigrateScriptOverrides(entity["Scripts"]);
            }
            root["WorldUnitsVersion"] = WorldUnitsVersion;
        }

        void MigratePrefabToCanvasUI(json& root) {
            if (root.value("CanvasUIVersion", 0) >= CanvasUIVersion || !root.contains("Entities"))
                return;
            json& entities = root["Entities"];
            for (size_t i = 0; i < entities.size(); ++i) {
                json& uiEntity = entities[i];
                if (!uiEntity.contains("UI Rect"))
                    continue;
                const std::string screen = uiEntity["UI Rect"].value("Screen", std::string("Top"));
                size_t canvasOwner = i;
                int parentIndex = uiEntity.value("Parent", -1);
                while (parentIndex >= 0 && parentIndex < static_cast<int>(entities.size()) && entities[parentIndex].contains("UI Rect")) {
                    canvasOwner = static_cast<size_t>(parentIndex);
                    parentIndex = entities[parentIndex].value("Parent", -1);
                }
                if (parentIndex >= 0 && parentIndex < static_cast<int>(entities.size()))
                    canvasOwner = static_cast<size_t>(parentIndex);
                if (!entities[canvasOwner].contains("Canvas"))
                    entities[canvasOwner]["Canvas"] = { { "Enabled", true }, { "Screen", screen }, { "Render Mode", "ScreenSpaceOverlay" }, { "Sort Order", 0 }, { "Scale Factor", 1.0f } };
            }
            root["CanvasUIVersion"] = CanvasUIVersion;
        }
    }

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
        rootJson["WorldUnitsVersion"] = WorldUnitsVersion;
        rootJson["CanvasUIVersion"] = CanvasUIVersion;
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

        const bool wasLegacyPixelPrefab = root.value("WorldUnitsVersion", 1) < WorldUnitsVersion;
        MigrateLegacyPixelPrefab(root);
        MigratePrefabToCanvasUI(root);
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

        if (wasLegacyPixelPrefab)
            Log::Info("PrefabSerializer: migrated legacy pixel prefab to Unity-style world units");
        Log::Info("Prefab instantiated from '" + path + "'");
        return subtreeRoot;
    }

}
