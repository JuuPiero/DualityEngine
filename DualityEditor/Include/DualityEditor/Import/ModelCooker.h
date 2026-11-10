#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DualityEditor/Import/ImportedModel.h"
#include "DualityEditor/Import/ModelImportSettings.h"
#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MeshLoader.h"

namespace Duality {

    // Turns ImportedModel pieces into the runtime assets both players load (desktop GL and
    // the 3DS PICA200 path): .dmesh meshes respecting the PICA limits, baked .anim clips and
    // Material v2 values. Pure functions -- no Assimp, no editor state -- so Tests/ can cover
    // every rule directly.
    namespace ModelCooker {

        // ---- Stable sub-asset identity ------------------------------------------------------
        // 32-hex GUID derived from the model GUID + a sub-asset key ("mesh:Body/Body#0",
        // "material:Skin", "clip:Run", "texture:...", "prefab"). Identical across re-imports, so
        // scene references to generated sub-assets never break.
        std::string StableGuid(const std::string& modelGuid, const std::string& key);

        // ---- Meshes -------------------------------------------------------------------------
        struct MeshCookOptions {
            float Scale = 1.0f;                // applied to positions + bone offset translations
            bool Skinned = true;               // false: Rig None -> static bind-pose mesh
            int MaxBonesPerVertex = 4;         // 1..DMeshMaxBonesPerVertex
            uint32_t MaxPaletteBones = DMeshMaxPaletteBones;
            uint32_t MaxVerticesPerSubMesh = DMeshMaxVerticesPerSubMesh;
            bool ImportVertexColors = true;
            float MinBoneWeight = 0.001f;      // smaller influences are dropped (Unity's default)
        };

        struct CookedSubMesh {
            std::vector<MeshVertex> Vertices; // unique, BoneIndices palette-local
            std::vector<uint16_t> Indices;    // into Vertices
            std::vector<uint16_t> Palette;    // global bone ids (CookedMesh::Bones)
        };

        struct CookedMesh {
            std::string Name;
            std::vector<MeshData::Bone> Bones;
            std::vector<CookedSubMesh> SubMeshes;
            glm::vec3 BoundsMin{ 0.0f }, BoundsMax{ 0.0f };
            float Radius = 0.0f;
            // Statistics for the 3DS budget report.
            uint32_t SourceVertices = 0, Triangles = 0, MaxPaletteSize = 0;
            uint32_t ClampedInfluenceVertices = 0; // vertices that had > MaxBonesPerVertex weights
            uint32_t UnweightedVertices = 0;       // skinned vertices bound to a fallback bone

            uint32_t UniqueVertexCount() const;
            uint32_t IndexCount() const;
            // What the players upload: the flat (non-indexed) triangle list, 72 bytes per corner.
            uint32_t RuntimeVertexCount() const { return IndexCount(); }
        };

        CookedMesh CookMesh(const ImportedMesh& mesh, const MeshCookOptions& options);

        struct DMeshWriteOptions {
            bool Quantize = false;
            bool ReadWrite = true;
        };
        std::vector<uint8_t> EncodeDMesh(const CookedMesh& mesh, const DMeshWriteOptions& options);
        // Writes atomically (temp file + rename) so a renderer never reads a half-written file.
        bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& bytes);
        bool WriteTextAtomic(const std::string& path, const std::string& text);

        // ---- Animation ----------------------------------------------------------------------
        struct ClipBakeOptions {
            float SampleRate = 30.0f;
            float Scale = 1.0f;              // applied to position keys (same as the meshes)
            bool KeyframeReduction = true;
            float PositionErrorPercent = 0.5f;
            float RotationErrorDegrees = 0.5f;
            float ScaleErrorPercent = 0.5f;
            std::string RootMotionNode;      // non-empty + RemoveRootMotion: XZ locked to frame 0
            bool RemoveRootMotion = false;
        };
        // Samples `clip`'s range of `take` at the sample rate (both ends included), then removes
        // keys the neighbouring keys reproduce within the error tolerances. Times start at 0.
        AnimationClipData BakeClip(const ImportedAnimation& take, const ModelClipSettings& clip, const ClipBakeOptions& options);
        // Removes interior keys whose value linear/slerp interpolation reproduces within
        // `tolerance` (absolute units / degrees). Always keeps the first and last key; a curve
        // that never moves collapses to one key.
        void ReduceKeys(std::vector<AnimationVec3Key>& keys, float tolerance);
        void ReduceKeys(std::vector<AnimationQuatKey>& keys, float toleranceDegrees);

        // ---- Materials ----------------------------------------------------------------------
        // Unity-like defaults: Lit/Diffuse (Lit/Specular when the file has a real specular
        // term, Unlit/Texture for unlit materials), back-face culled unless two-sided, Cutout for
        // alpha-masked, Transparent for blended / non-opaque materials.
        Material MapMaterial(const ImportedMaterial& source, const std::string& baseTextureGuid,
            const std::string& normalTextureGuid);

        // ---- 3DS budget -----------------------------------------------------------------------
        struct BudgetLimits {
            uint32_t TrianglesWarn = 10000;           // per model, ~30 fps with a few on screen
            uint32_t RuntimeVertexBytesWarn = 4u << 20; // linear heap: vertex buffers per model
            uint32_t TextureBytesWarn = 2u << 20;     // VRAM is 6 MB on the 3DS
            uint32_t TextureSizeWarn = 512;
            uint32_t DrawCallsWarn = 32;
        };
        // Bytes a texture takes on the 3DS for a tex3ds format name ("Auto" = RGBA8), including
        // a full mip chain when `mipmaps`.
        uint32_t N3DSTextureBytes(int width, int height, const std::string& format, bool mipmaps);
    }

}
