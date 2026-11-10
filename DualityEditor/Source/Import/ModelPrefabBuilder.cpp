#include "DualityEditor/Import/ModelPrefabBuilder.h"

#include <unordered_set>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace ModelPrefabBuilder {

        namespace {
            std::vector<AssetRef> ToRefs(const std::vector<std::string>& guids) {
                std::vector<AssetRef> refs;
                refs.reserve(guids.size());
                for (const std::string& guid : guids)
                    refs.push_back(AssetRef{ guid });
                return refs;
            }
        }

        // ---- MESH COMPONENTS: single place to switch to MeshFilter + MeshRenderer. -------------
        void AddStaticMeshComponents(Entity entity, const std::string& meshGuid, const std::vector<std::string>& materialGuids) {
            entity.AddComponent<MeshFilterComponent>().Mesh = AssetRef{ meshGuid };
            MeshRendererComponent& renderer = entity.AddComponent<MeshRendererComponent>();
            renderer.Materials = ToRefs(materialGuids);
        }

        bool Build(const ModelPrefabPlan& plan, const std::string& prefabPath) {
            if (plan.Nodes.empty())
                return false;
            Scene scene;
            std::vector<Entity> entities;
            entities.reserve(plan.Nodes.size());
            for (size_t i = 0; i < plan.Nodes.size(); ++i) {
                const ModelPrefabNode& node = plan.Nodes[i];
                Entity entity = scene.CreateEntity(node.Name.empty() ? std::string("Node") : node.Name);
                TransformComponent& transform = entity.GetComponent<TransformComponent>();
                transform.Translation = node.Translation;
                transform.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(node.Rotation)));
                transform.Scale = node.Scale;
                entities.push_back(entity);
                if (node.Parent >= 0 && node.Parent < static_cast<int>(i))
                    scene.SetParent(entity, entities[static_cast<size_t>(node.Parent)], {}, /*preserveWorldPosition=*/false);
            }

            for (size_t i = 0; i < plan.Nodes.size(); ++i) {
                const ModelPrefabNode& node = plan.Nodes[i];
                std::unordered_set<std::string> usedNames;
                for (const ModelPrefabMesh& mesh : node.Meshes) {
                    // One mesh: on the node itself (Unity). Several (a multi-material node split
                    // by the importer): one child object per mesh, identity transform.
                    Entity target = entities[i];
                    if (node.Meshes.size() > 1) {
                        std::string name = mesh.Name.empty() ? node.Name : mesh.Name;
                        for (int suffix = 1; !usedNames.insert(name).second; ++suffix)
                            name = (mesh.Name.empty() ? node.Name : mesh.Name) + " (" + std::to_string(suffix) + ")";
                        target = scene.CreateEntity(name);
                        scene.SetParent(target, entities[i], {}, /*preserveWorldPosition=*/false);
                    }
                    if (mesh.Skinned) {
                        SkinnedMeshRendererComponent& skin = target.AddComponent<SkinnedMeshRendererComponent>();
                        skin.Mesh = AssetRef{ mesh.MeshGuid };
                        skin.Materials = ToRefs(mesh.MaterialGuids);
                        for (int boneNode : mesh.BoneNodes) {
                            EntityRef ref;
                            if (boneNode >= 0 && boneNode < static_cast<int>(entities.size()))
                                ref.Handle = static_cast<uint32_t>(entities[static_cast<size_t>(boneNode)].Handle());
                            skin.Bones.push_back(ref);
                        }
                    } else {
                        AddStaticMeshComponents(target, mesh.MeshGuid, mesh.MaterialGuids);
                    }
                }
            }

            if (!plan.ClipGuid.empty()) {
                AnimationComponent& animation = entities.front().AddComponent<AnimationComponent>();
                animation.Clip = AssetRef{ plan.ClipGuid };
                animation.Loop = plan.ClipLoop;
            }
            return PrefabSerializer::Save(entities.front(), prefabPath);
        }
    }

}
