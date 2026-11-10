#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Duality {

    class Entity;

    // The model prefab (Unity's "model is a prefab"): one entity per source node with its rest
    // transform, mesh renderers on nodes that carry meshes, skinned renderers bound to the bone
    // entities, and an Animation component on the root when clips were imported.
    struct ModelPrefabMesh {
        std::string Name;
        std::string MeshGuid;
        std::vector<std::string> MaterialGuids;
        bool Skinned = false;
        std::vector<int> BoneNodes; // node index per MeshData::Bones entry (-1 = unresolved)
    };

    struct ModelPrefabNode {
        std::string Name;
        int Parent = -1;
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };
        std::vector<ModelPrefabMesh> Meshes;
    };

    struct ModelPrefabPlan {
        std::vector<ModelPrefabNode> Nodes; // [0] = root, parents before children
        std::string ClipGuid;               // first imported clip ("" = no Animation component)
        bool ClipLoop = true;
    };

    namespace ModelPrefabBuilder {
        // Builds the hierarchy in a scratch Scene and saves it with PrefabSerializer, so the
        // file is exactly what Create Prefab would have written for the same objects.
        bool Build(const ModelPrefabPlan& plan, const std::string& prefabPath);

        // ---- The one place that decides how a static mesh is attached to an entity. ----------
        // MeshFilter(Mesh) + MeshRenderer(Materials), like a Unity model prefab.
        void AddStaticMeshComponents(Entity entity, const std::string& meshGuid, const std::vector<std::string>& materialGuids);
    }

}
