#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Duality {

    // A Unity-style child/sub-asset discovered in a source model. It deliberately retains only
    // editor metadata here: source models remain the authoring truth, while MeshCooker will
    // later turn selected Mesh children into runtime .dmesh data for desktop/3DS.
    struct ModelSubAssetInfo {
        std::string Type;   // Mesh, Material, Texture, Animation
        std::string Name;
        std::string Detail; // vertex count, source texture URI, animation duration, ...
        unsigned int Index = 0;
        // Mesh/Animation children are real generated assets under Assets/.duality-import.
        // Material/Texture are inventory-only until their dedicated importers exist.
        std::string AssetGuid;
        std::string AssetPath;
    };

    struct ModelImportInfo {
        bool Valid = false;
        std::string Error;
        std::vector<ModelSubAssetInfo> Children;
    };

    // Desktop-only Assimp facade. Results are cached by source file timestamp/size and mirrored
    // into <model>.meta, so browsing a project never reparses an unchanged FBX/glTF each frame.
    class ModelImporter {
    public:
        static bool IsSupported(const std::filesystem::path& path);
        static const ModelImportInfo& Inspect(const std::filesystem::path& path);
        static void Invalidate(const std::filesystem::path& path);
    };

}
