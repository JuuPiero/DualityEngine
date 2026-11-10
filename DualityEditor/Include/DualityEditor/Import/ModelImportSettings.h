#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Duality {

    // Unity's ModelImporter settings (Model / Rig / Animation / Materials tabs), stored in the
    // source model's ".meta" as a "ModelImporterSettings" block next to "guid". Every key is
    // optional: a model imported before these settings existed reads the defaults below, which
    // reproduce what the first importer did (scale 1, 4 bones per vertex, one clip per take).
    enum class ModelNormalsMode { Import, Calculate, None };
    enum class ModelTangentsMode { Import, Calculate, None };
    enum class ModelRigType { None, Generic };
    enum class ModelMaterialCreation { Import, None };
    enum class ModelMeshCompression { Off, On };

    // One entry of the Animation tab's clip list (Unity's ModelImporterClipAnimation).
    struct ModelClipSettings {
        std::string Name;
        std::string Take;   // source animation (FBX take / glTF animation); empty = first take
        float Start = 0.0f; // seconds into the take
        float End = -1.0f;  // seconds; < 0 = end of the take
        bool Loop = false;  // Unity's Loop Time

        bool operator==(const ModelClipSettings& other) const {
            return Name == other.Name && Take == other.Take && Start == other.Start && End == other.End && Loop == other.Loop;
        }
    };

    struct ModelImportSettings {
        // ---- Model ----------------------------------------------------------------------
        float ScaleFactor = 1.0f;
        // Unity's Convert Units: also scale by the file's unit (FBX centimetres -> metres).
        bool ConvertUnits = true;
        ModelNormalsMode Normals = ModelNormalsMode::Import;
        float SmoothingAngle = 60.0f; // degrees, used when normals are calculated
        // Tangents are never stored in .dmesh (the loader derives them for normal mapping);
        // this only controls whether Assimp computes them for the import-time validation.
        ModelTangentsMode Tangents = ModelTangentsMode::Calculate;
        bool OptimizeMesh = true;                                  // vertex/index order locality
        ModelMeshCompression MeshCompression = ModelMeshCompression::Off; // 16/8-bit .dmesh stream
        bool ReadWrite = true;
        bool ImportVertexColors = true;
        // Textures extracted from the model (embedded or next to it) are resized to a power of
        // two no larger than this, then cooked by tex3ds in "3DS Format" for the 3DS build.
        int MaxTextureSize = 512;
        std::string TextureFormat = "Auto"; // Auto | RGBA8 | RGB8 | RGBA5551 | RGB565 | RGBA4 | ETC1 | ETC1A4

        // ---- Rig ------------------------------------------------------------------------
        ModelRigType Rig = ModelRigType::Generic;
        int MaxBonesPerVertex = 4; // 1..4 (PICA/GL skinning shaders read four influences)

        // ---- Animation ------------------------------------------------------------------
        bool ImportAnimation = true;
        float SampleRate = 30.0f;     // baked keys per second
        bool KeyframeReduction = true;
        float PositionError = 0.5f;   // percent of the channel's motion range
        float RotationError = 0.5f;   // degrees
        float ScaleError = 0.5f;      // percent
        // In-place clips: the root bone's horizontal (XZ) motion is removed from every clip.
        bool RemoveRootMotion = false;
        std::vector<ModelClipSettings> Clips; // empty = one clip per take, full length

        // ---- Materials ------------------------------------------------------------------
        ModelMaterialCreation MaterialCreation = ModelMaterialCreation::Import;
        // Source material name -> ".mat" GUID used instead of the generated material
        // (Unity's "remapped materials", filled by Extract Materials or by hand).
        std::map<std::string, std::string> MaterialRemaps;
        // Texture key (see ModelImporter's inventory) -> texture GUID (Extract Textures).
        std::map<std::string, std::string> TextureRemaps;

        nlohmann::json ToJson() const;
        static ModelImportSettings FromJson(const nlohmann::json& value);
        // FNV-1a of ToJson(): stored with the import results to detect setting changes.
        std::string Hash() const;
        bool operator==(const ModelImportSettings& other) const { return ToJson() == other.ToJson(); }
        bool operator!=(const ModelImportSettings& other) const { return !(*this == other); }

        // <modelPath>.meta "ModelImporterSettings" (defaults when absent). Save keeps every other key.
        static ModelImportSettings Load(const std::string& modelPath);
        static bool Save(const std::string& modelPath, const ModelImportSettings& settings);

        static const char* const* TextureFormatNames(int& count);
    };

    // Small shared helpers (also used by the cooker's stable GUIDs and content hashes).
    uint64_t Fnv1a64(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
    std::string HexString(uint64_t value);

}
