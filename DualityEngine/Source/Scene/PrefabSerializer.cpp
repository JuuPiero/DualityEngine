#include "DualityEngine/Scene/PrefabSerializer.h"

#include <cctype>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/ModelAsset.h"
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

        // EntityRef values serialize as { "$entity": rawHandle } (including inside script
        // property overrides/nested fields). A duplicate must preserve references inside its
        // own copied subtree rather than pointing those fields back at the source objects.
        void RemapEntityReferences(json& value, const std::unordered_map<uint32_t, uint32_t>& remap) {
            if (value.is_object()) {
                auto entityRef = value.find("$entity");
                if (entityRef != value.end() && entityRef->is_number_unsigned()) {
                    const uint32_t oldHandle = entityRef->get<uint32_t>();
                    const auto replacement = remap.find(oldHandle);
                    if (replacement != remap.end())
                        *entityRef = replacement->second;
                }
                for (auto& [key, child] : value.items()) {
                    (void)key;
                    RemapEntityReferences(child, remap);
                }
            } else if (value.is_array()) {
                for (json& child : value)
                    RemapEntityReferences(child, remap);
            }
        }

        void DropRawEntityHandles(json& value) {
            if (value.is_object()) {
                if (value.contains("$entity")) {
                    value = nullptr;
                    return;
                }
                for (auto& item : value.items())
                    DropRawEntityHandles(item.value());
            } else if (value.is_array()) {
                for (json& item : value)
                    DropRawEntityHandles(item);
            }
        }

        // Clipboard encoding of references that point outside the copied subtrees: they are
        // kept as {"$external": rawHandle} so Paste can reconnect them (Unity keeps references to
        // other scene objects when pasting into the same scene).
        void EncodeClipboardReferences(json& value, const std::unordered_map<entt::entity, int>& indexOf) {
            if (value.is_object()) {
                if (value.size() == 1 && value.contains("$entity")) {
                    const entt::entity handle = static_cast<entt::entity>(value["$entity"].get<uint32_t>());
                    auto it = indexOf.find(handle);
                    if (it != indexOf.end())
                        value = it->second;
                    else
                        value = json{ { "$external", static_cast<uint32_t>(handle) } };
                    return;
                }
                for (auto& item : value.items())
                    EncodeClipboardReferences(item.value(), indexOf);
            } else if (value.is_array()) {
                for (json& item : value)
                    EncodeClipboardReferences(item, indexOf);
            }
        }

        // Rewrites {"$external": h} into an index into `table` (appending the resolved target),
        // so the regular index resolver handles internal and external references uniformly.
        void DecodeClipboardExternals(json& value, Scene& scene, const std::unordered_map<uint32_t, uint32_t>* remap,
            std::vector<Entity>& table, std::unordered_map<uint32_t, int>& tableIndex) {
            if (value.is_object()) {
                if (value.size() == 1 && value.contains("$external")) {
                    uint32_t handle = value["$external"].get<uint32_t>();
                    if (remap) {
                        auto mapped = remap->find(handle);
                        handle = mapped == remap->end() ? EntityRef::Invalid : mapped->second;
                    }
                    auto known = tableIndex.find(handle);
                    if (known == tableIndex.end()) {
                        const entt::entity target = static_cast<entt::entity>(handle);
                        Entity resolved = (handle != EntityRef::Invalid && scene.Registry().valid(target)) ? Entity(target, &scene) : Entity{};
                        known = tableIndex.emplace(handle, static_cast<int>(table.size())).first;
                        table.push_back(resolved);
                    }
                    value = known->second;
                    return;
                }
                for (auto& item : value.items())
                    DecodeClipboardExternals(item.value(), scene, remap, table, tableIndex);
            } else if (value.is_array()) {
                for (json& item : value)
                    DecodeClipboardExternals(item, scene, remap, table, tableIndex);
            }
        }

        std::string MakeDuplicateName(Scene& scene, Entity source) {
            std::string baseName = source.GetComponent<NameComponent>().Name;
            // Repeating Unity's naming rule is friendlier than producing
            // "Tree (1) (1)" when the currently selected copy is duplicated.
            const size_t suffixStart = baseName.rfind(" (");
            if (suffixStart != std::string::npos && !baseName.empty() && baseName.back() == ')') {
                bool numericSuffix = suffixStart + 2 < baseName.size();
                for (size_t i = suffixStart + 2; numericSuffix && i + 1 < baseName.size(); ++i)
                    numericSuffix = std::isdigit(static_cast<unsigned char>(baseName[i])) != 0;
                if (numericSuffix)
                    baseName.resize(suffixStart);
            }
            const Entity parent = source.GetComponent<HierarchyComponent>().Parent;
            const std::vector<Entity>& siblings = parent
                ? parent.GetComponent<HierarchyComponent>().Children
                : scene.GetRootEntities();
            auto exists = [&](const std::string& candidate) {
                for (Entity sibling : siblings) {
                    if (sibling && sibling.GetComponent<NameComponent>().Name == candidate)
                        return true;
                }
                return false;
            };
            for (int suffix = 1;; ++suffix) {
                const std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
                if (!exists(candidate))
                    return candidate;
            }
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
            // Store EntityRefs as indices into this prefab (references to entities outside the
            // saved subtree become "none", exactly like Unity prefab assets cannot reference
            // scene objects).
            EncodeEntityReferenceIndices(entityJson, indexOf);

            // A prefab asset is its own source of truth; serializing an instance link into
            // it would make every newly-created root point back at itself.
            entityJson.erase("Prefab Instance");

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
        rootJson["MeshFilterVersion"] = 1; // see SceneSerializer::SerializeToJson
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

    Entity PrefabSerializer::Instantiate(Scene& scene, const std::string& path, Entity parent,
        const AssetRef& sourcePrefab) {
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
        // Prefabs written before EntityRefs were index-encoded stored the raw EnTT handle of the
        // editing session ({"$entity": handle}); that number means nothing in another Scene, so
        // treat it as "none" instead of letting it alias an arbitrary entity.
        DropRawEntityHandles(root["Entities"]);

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
        // EntityRefs now hold prefab-local indices -> this instance's own entities.
        ResolveLoadedEntityReferences(scene, orderedEntities);

        // Index 0 is always the prefab's own root -- Save's collect() visits it first,
        // before any descendant.
        Entity subtreeRoot = orderedEntities[0];
        scene.SetParent(subtreeRoot, parent);
        if (!sourcePrefab.Guid.empty()) {
            if (subtreeRoot.HasComponent<PrefabInstanceComponent>())
                subtreeRoot.GetComponent<PrefabInstanceComponent>().Prefab = sourcePrefab;
            else
                subtreeRoot.AddComponent<PrefabInstanceComponent>(PrefabInstanceComponent{ sourcePrefab });
        }

        if (wasLegacyPixelPrefab)
            Log::Info("PrefabSerializer: migrated legacy pixel prefab to Unity-style world units");
        Log::Info("Prefab instantiated from '" + path + "'");
        return subtreeRoot;
    }

    bool PrefabSerializer::Apply(Entity instanceRoot) {
        if (!instanceRoot || !instanceRoot.HasComponent<PrefabInstanceComponent>())
            return false;
        const std::string path = AssetDatabase::ResolvePath(
            instanceRoot.GetComponent<PrefabInstanceComponent>().Prefab.Guid);
        if (path.empty()) {
            Log::Error("Prefab apply failed: source asset cannot be resolved");
            return false;
        }
        // A model (FBX/glTF) instance links to its source file, which is not a prefab JSON:
        // like Unity, model prefabs are read-only -- change the import settings instead.
        if (ModelAsset::IsModelFile(path)) {
            Log::Warn("Prefab apply: '" + path + "' is a model; model prefabs are read-only (edit its import settings, or Unpack)");
            return false;
        }
        return Save(instanceRoot, path);
    }

    Entity PrefabSerializer::Revert(Scene& scene, Entity instanceRoot) {
        if (!instanceRoot || instanceRoot.GetScene() != &scene ||
            !instanceRoot.HasComponent<PrefabInstanceComponent>())
            return Entity{};

        const AssetRef source = instanceRoot.GetComponent<PrefabInstanceComponent>().Prefab;
        // .prefab assets resolve to themselves; a model resolves to its generated prefab.
        const std::string path = ModelAsset::ResolveInstantiablePrefab(source.Guid);
        if (path.empty()) {
            Log::Error("Prefab revert failed: source asset cannot be resolved");
            return Entity{};
        }

        const Entity parent = instanceRoot.GetComponent<HierarchyComponent>().Parent;
        const auto& siblings = parent ? parent.GetComponent<HierarchyComponent>().Children : scene.GetRootEntities();
        const auto it = std::find(siblings.begin(), siblings.end(), instanceRoot);
        const std::size_t siblingIndex = it == siblings.end() ? siblings.size()
            : static_cast<std::size_t>(std::distance(siblings.begin(), it));
        scene.DestroyEntity(instanceRoot);
        Entity replacement = Instantiate(scene, path, parent, source);
        if (replacement)
            scene.SetSiblingIndex(replacement, parent, siblingIndex, false);
        return replacement;
    }

    bool PrefabSerializer::Unpack(Entity instanceRoot) {
        if (!instanceRoot || !instanceRoot.HasComponent<PrefabInstanceComponent>())
            return false;
        instanceRoot.RemoveComponent<PrefabInstanceComponent>();
        return true;
    }

    Entity PrefabSerializer::Duplicate(Scene& scene, Entity source) {
        if (!source || source.GetScene() != &scene || !scene.Registry().valid(source.Handle()))
            return Entity{};

        std::vector<Entity> sources;
        std::unordered_map<entt::entity, int> sourceIndex;
        std::function<void(Entity)> collect = [&](Entity entity) {
            sourceIndex[entity.Handle()] = static_cast<int>(sources.size());
            sources.push_back(entity);
            for (Entity child : entity.GetComponent<HierarchyComponent>().Children)
                collect(child);
        };
        collect(source);

        json entities = json::array();
        for (Entity entity : sources) {
            json entityJson;
            SerializeEntityComponents(entity, entityJson);
            entityJson["Parent"] = entity == source
                ? -1
                : sourceIndex[entity.GetComponent<HierarchyComponent>().Parent.Handle()];
            entities.push_back(std::move(entityJson));
        }

        // Allocate the entire subtree first so all intra-subtree EntityRefs have a destination
        // before any component/script field is deserialized.
        std::vector<Entity> copies;
        copies.reserve(sources.size());
        std::unordered_map<uint32_t, uint32_t> handleRemap;
        handleRemap.reserve(sources.size());
        for (Entity original : sources) {
            Entity copy = scene.CreateEntity();
            handleRemap[static_cast<uint32_t>(original.Handle())] = static_cast<uint32_t>(copy.Handle());
            copies.push_back(copy);
        }

        for (size_t i = 0; i < copies.size(); ++i) {
            RemapEntityReferences(entities[i], handleRemap);
            DeserializeEntityComponents(copies[i], entities[i]);
        }
        for (size_t i = 1; i < copies.size(); ++i) {
            const int parentIndex = entities[i].value("Parent", -1);
            if (parentIndex >= 0 && parentIndex < static_cast<int>(copies.size()))
                scene.SetParent(copies[i], copies[parentIndex], {}, /*preserveWorldPosition=*/false);
        }

        Entity copyRoot = copies.front();
        const Entity sourceParent = source.GetComponent<HierarchyComponent>().Parent;
        scene.SetParent(copyRoot, sourceParent, source, /*preserveWorldPosition=*/false);
        copyRoot.GetComponent<NameComponent>().Name = MakeDuplicateName(scene, source);
        Log::Info("Duplicated entity '" + source.GetComponent<NameComponent>().Name + "'");
        return copyRoot;
    }

    json PrefabSerializer::CopyToJson(Scene& scene, const std::vector<Entity>& roots) {
        std::vector<Entity> ordered;
        std::unordered_map<entt::entity, int> indexOf;
        std::vector<int> rootIndices;
        std::function<void(Entity)> collect = [&](Entity entity) {
            if (indexOf.count(entity.Handle()))
                return;
            indexOf[entity.Handle()] = static_cast<int>(ordered.size());
            ordered.push_back(entity);
            for (Entity child : entity.GetComponent<HierarchyComponent>().Children)
                collect(child);
        };
        for (Entity root : roots) {
            if (!root || root.GetScene() != &scene || indexOf.count(root.Handle()))
                continue;
            rootIndices.push_back(static_cast<int>(ordered.size()));
            collect(root);
        }

        json entities = json::array();
        for (Entity entity : ordered) {
            json entityJson;
            SerializeEntityComponents(entity, entityJson);
            EncodeClipboardReferences(entityJson, indexOf);
            const Entity parent = entity.GetComponent<HierarchyComponent>().Parent;
            auto parentIndex = parent ? indexOf.find(parent.Handle()) : indexOf.end();
            entityJson["Parent"] = parentIndex == indexOf.end() ? -1 : parentIndex->second;
            entities.push_back(std::move(entityJson));
        }

        json rootsJson = json::array();
        for (int index : rootIndices) {
            Entity root = ordered[static_cast<size_t>(index)];
            const TransformComponent world = scene.GetWorldTransform(root);
            const Entity parent = root.GetComponent<HierarchyComponent>().Parent;
            rootsJson.push_back({
                { "Index", index },
                { "SourceParent", parent ? static_cast<uint32_t>(parent.Handle()) : EntityRef::Invalid },
                { "WorldTranslation", { world.Translation.x, world.Translation.y, world.Translation.z } },
                { "WorldRotation", { world.Rotation.x, world.Rotation.y, world.Rotation.z } },
                { "WorldScale", { world.Scale.x, world.Scale.y, world.Scale.z } },
            });
        }

        json clipboard;
        clipboard["DualityClipboard"] = 1;
        clipboard["WorldUnitsVersion"] = WorldUnitsVersion;
        clipboard["CanvasUIVersion"] = CanvasUIVersion;
        clipboard["MeshFilterVersion"] = 1;
        clipboard["Entities"] = std::move(entities);
        clipboard["Roots"] = std::move(rootsJson);
        return clipboard;
    }

    std::vector<Entity> PrefabSerializer::PasteFromJson(Scene& scene, const json& clipboard, Entity parent,
        const std::unordered_map<uint32_t, uint32_t>* externalRemap) {
        std::vector<Entity> pastedRoots;
        if (!clipboard.is_object() || !clipboard.contains("Entities") || !clipboard["Entities"].is_array() ||
            clipboard["Entities"].empty())
            return pastedRoots;

        json entities = clipboard["Entities"];
        std::vector<Entity> created;
        created.reserve(entities.size());
        for (size_t i = 0; i < entities.size(); ++i)
            created.push_back(scene.CreateEntity());

        // Index table = the pasted entities followed by every external reference target.
        std::vector<Entity> table = created;
        std::unordered_map<uint32_t, int> externalIndex;
        for (json& entityJson : entities)
            DecodeClipboardExternals(entityJson, scene, externalRemap, table, externalIndex);

        for (size_t i = 0; i < entities.size(); ++i)
            DeserializeEntityComponents(created[i], entities[i]);
        for (size_t i = 0; i < entities.size(); ++i) {
            const int parentIndex = entities[i].value("Parent", -1);
            if (parentIndex >= 0 && parentIndex < static_cast<int>(created.size()))
                scene.SetParent(created[i], created[static_cast<size_t>(parentIndex)], {}, /*preserveWorldPosition=*/false);
        }
        ResolveLoadedEntityReferences(scene, created, table);

        auto readVec3 = [](const json& value, glm::vec3 fallback) {
            if (!value.is_array() || value.size() != 3)
                return fallback;
            return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
        };
        const json roots = clipboard.value("Roots", json::array());
        for (const json& rootJson : roots) {
            const int index = rootJson.value("Index", -1);
            if (index < 0 || index >= static_cast<int>(created.size()))
                continue;
            Entity root = created[static_cast<size_t>(index)];
            // Keep the copied object exactly where it was in the world, whatever parent it is
            // pasted under (Unity's Paste / Paste As Child behaviour).
            auto& transform = root.GetComponent<TransformComponent>();
            if (rootJson.contains("WorldTranslation")) {
                transform.Translation = readVec3(rootJson["WorldTranslation"], transform.Translation);
                transform.Rotation = readVec3(rootJson["WorldRotation"], transform.Rotation);
                transform.Scale = readVec3(rootJson["WorldScale"], transform.Scale);
            }
            if (parent && parent.GetScene() == &scene)
                scene.SetParent(root, parent, {}, /*preserveWorldPosition=*/true);
            pastedRoots.push_back(root);
        }
        return pastedRoots;
    }

}
