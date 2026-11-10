#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Duality {

    // A Unity-style child/sub-asset of a source model, shown under it in the Project window.
    // Every cooked child is a real generated asset under <modelDir>/.duality-import/<modelGuid>/
    // with a GUID derived from the model GUID + Key, so it is identical across re-imports.
    struct ModelSubAssetInfo {
        std::string Type;   // Mesh, Material, Texture, Animation, Avatar
        std::string Name;
        std::string Detail; // vertex count, texture size, clip length, ...
        unsigned int Index = 0;
        std::string AssetGuid; // empty = inventory only (Avatar, or a remapped/external asset shown for reference)
        std::string AssetPath;
        std::string Key;       // stable identity ("mesh:Root/Body#0", "material:Skin", ...)
    };

    // 3DS budget of one import (Inspector "3DS Budget" report).
    struct ModelBudgetMesh {
        std::string Name;
        uint32_t Vertices = 0;        // unique (indexed) vertices
        uint32_t Triangles = 0;
        uint32_t RuntimeVertices = 0; // flat triangle-list vertices both players upload
        uint32_t RuntimeBytes = 0;    // RuntimeVertices * sizeof(MeshVertex)
        uint32_t FileBytes = 0;
        uint32_t SubMeshes = 0;       // draw calls
        uint32_t Bones = 0;
        uint32_t MaxPaletteBones = 0;
        uint32_t ClampedInfluences = 0;
    };
    struct ModelBudgetTexture {
        std::string Name;
        int SourceWidth = 0, SourceHeight = 0, Width = 0, Height = 0;
        std::string Format;
        uint32_t Bytes = 0; // on the 3DS, mip chain included
    };
    struct ModelBudget {
        std::vector<ModelBudgetMesh> Meshes;
        std::vector<ModelBudgetTexture> Textures;
        uint32_t Triangles = 0, RuntimeVertexBytes = 0, TextureBytes = 0, DrawCalls = 0, Bones = 0, Clips = 0, ClipKeys = 0;
        std::vector<std::string> Warnings;
    };

    struct ModelImportInfo {
        bool Valid = false;
        std::string Error;
        std::vector<ModelSubAssetInfo> Children;
        std::string PrefabPath;                  // generated model prefab ("" = none)
        ModelBudget Budget;
        std::vector<std::string> SourceMaterials; // material names, for the remap table
        struct Take { std::string Name; float Duration = 0.0f; };
        std::vector<Take> SourceTakes;            // animations in the file, for the clip list
        std::string RootMotionNode;
        float FileUnitScale = 1.0f;
        std::vector<std::string> Warnings;
    };

    enum class ModelImportState { Unknown, Queued, Importing, Imported, Failed };

    // Desktop-only Assimp importer (Unity's ModelImporter). A model appearing in Assets (OS drop
    // onto the editor, copied in, or changed on disk) is imported on a worker thread and baked
    // into 3DS-ready assets: .dmesh meshes (PICA200 limits), .mat materials, power-of-two .png
    // textures, sampled + reduced .anim clips and a model prefab. Results are mirrored into
    // <model>.meta ("ModelImporter" block) with the source hash + settings hash, so unchanged
    // models are never re-parsed.
    class ModelImporter {
    public:
        static bool IsSupported(const std::filesystem::path& path);

        // Never blocks: returns the last import result (or what <model>.meta recorded) and
        // queues a background (re)import when the source / settings / importer version changed.
        // The returned reference stays valid until the next Tick().
        static const ModelImportInfo& Inspect(const std::filesystem::path& path);
        static ModelImportState GetState(const std::filesystem::path& path, float* progress = nullptr);
        // Queue a re-import (Inspector Apply / Reimport, a dropped file replacing an old one).
        static void RequestImport(const std::filesystem::path& path, bool force = true);
        static void Invalidate(const std::filesystem::path& path);

        // Synchronous import on the calling thread (tests, command-line builds). Returns true on
        // success; the result is immediately visible through Inspect().
        static bool ImportNow(const std::filesystem::path& path, bool force = false);
        // For the 3DS build thread: makes sure `path` is imported and up to date. Waits for the
        // editor's worker/main thread when they are running, else imports inline.
        static void EnsureImported(const std::filesystem::path& path);

        // Main thread, once per frame: finalizes finished jobs (AssetDatabase, prefab, .meta)
        // and every ~2 s scans `assetsRoot` for new / changed / deleted models.
        static void Tick(const std::filesystem::path& assetsRoot);
        // Called on the main thread after a model's outputs were rewritten (renderer caches).
        static void SetReimportListener(std::function<void(const std::string& modelPath)> listener);

        // Progress UI / automation.
        static int PendingJobCount();
        static std::string ActiveJobLabel(float* progress = nullptr);
        static bool IsIdle();

        // Extract Materials / Extract Textures (Unity): moves the generated sub-assets out of
        // .duality-import into `targetFolder` as editable assets -- keeping their GUIDs, so
        // every existing reference follows -- records the remaps and re-imports.
        static int ExtractMaterials(const std::filesystem::path& modelPath, const std::filesystem::path& targetFolder);
        static int ExtractTextures(const std::filesystem::path& modelPath, const std::filesystem::path& targetFolder);

        // Importer behaviour version; bump to re-import every model on the next scan.
        static constexpr int ImporterVersion = 2;
    };

}
