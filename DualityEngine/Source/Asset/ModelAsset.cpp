#include "DualityEngine/Asset/ModelAsset.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "DualityEngine/Asset/AssetDatabase.h"

namespace Duality {

    namespace ModelAsset {

        namespace {
            std::string LowerExtension(const std::string& path) {
                std::string extension = std::filesystem::path(path).extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return extension;
            }
        }

        bool IsModelFile(const std::string& path) {
            const std::string extension = LowerExtension(path);
            return extension == ".fbx" || extension == ".gltf" || extension == ".glb" || extension == ".obj";
        }

        std::string ImportDirectory(const std::string& modelPath, const std::string& modelGuid) {
            if (modelGuid.empty())
                return {};
            return (std::filesystem::path(modelPath).parent_path() / ".duality-import" / modelGuid).string();
        }

        std::string GeneratedPrefabPath(const std::string& modelPath, const std::string& modelGuid) {
            const std::string directory = ImportDirectory(modelPath, modelGuid);
            return directory.empty() ? std::string() : (std::filesystem::path(directory) / "model.prefab").string();
        }

        std::string ResolveInstantiablePrefab(const std::string& guid) {
            const std::string path = AssetDatabase::ResolvePath(guid);
            if (path.empty())
                return {};
            if (LowerExtension(path) == ".prefab")
                return path;
            if (!IsModelFile(path))
                return {};
            std::error_code error;
            const std::string prefab = GeneratedPrefabPath(path, guid);
            return std::filesystem::is_regular_file(prefab, error) ? prefab : std::string();
        }
    }

}
