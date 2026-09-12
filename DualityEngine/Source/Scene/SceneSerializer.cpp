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

    namespace {
        // Version 1 stored all gameplay values in screen pixels. Version 2 follows Unity's
        // convention: scenes store world units and PPU is applied only by the 2D renderer.
        // This migration happens in-memory so opening an old project keeps its appearance; the
        // next ordinary Save Scene writes the compact, unit-based version permanently.
        constexpr int WorldUnitsVersion = 2;
        constexpr float LegacyPixelsPerUnit = 100.0f;
        constexpr int CanvasUIVersion = 1;

        void ScaleNumber(json& value) {
            if (value.is_number())
                value = value.get<float>() / LegacyPixelsPerUnit;
        }

        void ScaleVector(json& value) {
            if (!value.is_array())
                return;
            for (json& element : value)
                ScaleNumber(element);
        }

        void ScaleFields(json& component, std::initializer_list<const char*> names) {
            for (const char* name : names) {
                if (component.contains(name))
                    ScaleVector(component[name]);
            }
        }

        void ScaleScalarFields(json& component, std::initializer_list<const char*> names) {
            for (const char* name : names) {
                if (component.contains(name))
                    ScaleNumber(component[name]);
            }
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
                    ScaleScalarFields(overrides, { "MoveSpeed", "JumpSpeed" });
                else if (className == "PatrolEnemyBehaviour")
                    ScaleScalarFields(overrides, { "Speed", "Range" });
                else if (className == "FeatureShowcaseBehaviour")
                    ScaleScalarFields(overrides, { "MoveSpeed" });
                else if (className == "BounceBehaviour")
                    ScaleScalarFields(overrides, { "Amplitude" });
            }
        }

        void MigrateLegacyPixelScene(json& root) {
            if (root.value("WorldUnitsVersion", 1) >= WorldUnitsVersion || !root.contains("Entities"))
                return;

            for (json& entity : root["Entities"]) {
                if (entity.contains("Transform"))
                    ScaleFields(entity["Transform"], { "Translation" });
                if (entity.contains("Sprite Renderer"))
                    ScaleFields(entity["Sprite Renderer"], { "Size" });
                if (entity.contains("Box Collider 2D"))
                    ScaleFields(entity["Box Collider 2D"], { "Offset", "Size" });
                if (entity.contains("Circle Collider 2D")) {
                    ScaleFields(entity["Circle Collider 2D"], { "Offset" });
                    ScaleScalarFields(entity["Circle Collider 2D"], { "Radius" });
                }
                if (entity.contains("Capsule Collider 2D")) {
                    ScaleFields(entity["Capsule Collider 2D"], { "Offset" });
                    ScaleScalarFields(entity["Capsule Collider 2D"], { "Radius", "Height" });
                }
                if (entity.contains("Polygon Collider 2D"))
                    ScaleFields(entity["Polygon Collider 2D"], { "Offset", "Vertex 0", "Vertex 1", "Vertex 2", "Vertex 3", "Vertex 4", "Vertex 5", "Vertex 6", "Vertex 7" });
                if (entity.contains("Box Collider 3D"))
                    ScaleFields(entity["Box Collider 3D"], { "Offset", "Size" });
                if (entity.contains("Sphere Collider 3D")) {
                    ScaleFields(entity["Sphere Collider 3D"], { "Offset" });
                    ScaleScalarFields(entity["Sphere Collider 3D"], { "Radius" });
                }
                if (entity.contains("Capsule Collider 3D")) {
                    ScaleFields(entity["Capsule Collider 3D"], { "Offset" });
                    ScaleScalarFields(entity["Capsule Collider 3D"], { "Radius", "Height" });
                }
                if (entity.contains("Tilemap"))
                    ScaleFields(entity["Tilemap"], { "Cell Size" });
                if (entity.contains("Line Renderer")) {
                    ScaleScalarFields(entity["Line Renderer"], { "Width" });
                    ScaleFields(entity["Line Renderer"], { "Point 0", "Point 1", "Point 2", "Point 3", "Point 4", "Point 5", "Point 6", "Point 7" });
                }
                if (entity.contains("Follow Target"))
                    ScaleFields(entity["Follow Target"], { "Offset" });
                if (entity.contains("Particle System")) {
                    ScaleScalarFields(entity["Particle System"], { "Start Speed", "Start Size" });
                    ScaleFields(entity["Particle System"], { "Gravity", "Velocity Spread" });
                }
                if (entity.contains("Audio Source"))
                    ScaleScalarFields(entity["Audio Source"], { "Min Distance", "Max Distance" });
                if (entity.contains("Scripts"))
                    MigrateScriptOverrides(entity["Scripts"]);
            }
            root["WorldUnitsVersion"] = WorldUnitsVersion;
        }

        void MigrateSceneToCanvasUI(json& root) {
            if (root.value("CanvasUIVersion", 0) >= CanvasUIVersion || !root.contains("Entities"))
                return;

            json& entities = root["Entities"];
            for (size_t i = 0; i < entities.size(); ++i) {
                json& uiEntity = entities[i];
                if (!uiEntity.contains("UI Rect"))
                    continue;

                const std::string legacyScreen = uiEntity["UI Rect"].value("Screen", std::string("Top"));
                size_t canvasOwner = i;
                int parentIndex = uiEntity.value("Parent", -1);
                while (parentIndex >= 0 && parentIndex < static_cast<int>(entities.size()) && entities[parentIndex].contains("UI Rect")) {
                    canvasOwner = static_cast<size_t>(parentIndex);
                    parentIndex = entities[parentIndex].value("Parent", -1);
                }
                if (parentIndex >= 0 && parentIndex < static_cast<int>(entities.size()))
                    canvasOwner = static_cast<size_t>(parentIndex);

                json& canvasEntity = entities[canvasOwner];
                if (!canvasEntity.contains("Canvas")) {
                    canvasEntity["Canvas"] = {
                        { "Enabled", true },
                        { "Screen", legacyScreen },
                        { "Render Mode", "ScreenSpaceOverlay" },
                        { "Sort Order", 0 },
                        { "Scale Factor", 1.0f }
                    };
                }
            }
            root["CanvasUIVersion"] = CanvasUIVersion;
        }
    }

    json SceneSerializer::SerializeToJson() {
        json root;
        root["WorldUnitsVersion"] = WorldUnitsVersion;
        root["CanvasUIVersion"] = CanvasUIVersion;
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
        return root;
    }

    bool SceneSerializer::DeserializeFromJson(const json& root) {
        json normalizedRoot = root;
        const bool wasLegacyPixelScene = normalizedRoot.value("WorldUnitsVersion", 1) < WorldUnitsVersion;
        MigrateLegacyPixelScene(normalizedRoot);
        MigrateSceneToCanvasUI(normalizedRoot);
        if (!normalizedRoot.contains("Entities"))
            return false;

        // First pass: create every entity and hydrate its registered components, in
        // file order -- `orderedEntities` ends up index-aligned with root["Entities"]
        // itself, which the second pass below relies on to resolve "Parent" indices
        // (entt::entity handles are freshly assigned by CreateEntity, so they can't
        // be known ahead of time; this is exactly why Serialize wrote indices
        // instead of handles).
        std::vector<Entity> orderedEntities;
        for (auto& entityJson : normalizedRoot["Entities"]) {
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
            int parentIndex = normalizedRoot["Entities"][i].value("Parent", -1);
            if (parentIndex >= 0 && parentIndex < static_cast<int>(orderedEntities.size()))
                m_Scene.SetParent(orderedEntities[i], orderedEntities[parentIndex], {}, /*preserveWorldPosition=*/false);
        }

        if (wasLegacyPixelScene)
            Log::Info("SceneSerializer: migrated legacy pixel scene to Unity-style world units (save to persist)");
        return true;
    }

    bool SceneSerializer::Serialize(const std::string& path) {
        json root = SerializeToJson();

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

        if (!DeserializeFromJson(root))
            return false;

        Log::Info("Scene loaded from '" + path + "'");
        return true;
    }

}
