#pragma once

#include <string>

namespace Duality {

    // Naming contract between the editor's model importer (DualityEditor/Source/ModelImporter.cpp)
    // and engine code that only needs to *find* what it generated. A source model
    // (<dir>/Hero.fbx with GUID g) keeps everything it cooks under <dir>/.duality-import/g/:
    // meshes (.dmesh), clips (.anim), materials (.mat), textures (.png) and the model prefab
    // (model.prefab), Unity's model-as-prefab. Assimp itself never ships with the engine; on the
    // 3DS only those cooked files are loaded.
    namespace ModelAsset {

        // .fbx / .gltf / .glb / .obj (case-insensitive).
        bool IsModelFile(const std::string& path);

        // "<modelDir>/.duality-import/<modelGuid>" (generic '/' separators kept from `modelPath`).
        std::string ImportDirectory(const std::string& modelPath, const std::string& modelGuid);
        std::string GeneratedPrefabPath(const std::string& modelPath, const std::string& modelGuid);

        // What a prefab-instance link (PrefabInstanceComponent::Prefab) instantiates from: a
        // ".prefab" asset itself, or a model's generated prefab. Empty when neither exists.
        std::string ResolveInstantiablePrefab(const std::string& guid);
    }

}
