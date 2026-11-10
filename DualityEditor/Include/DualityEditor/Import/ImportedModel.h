#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Duality {

    // Assimp-free snapshot of a source model, filled by ModelImporter (from aiScene) and
    // consumed by ModelCooker. Keeping the cooker on this plain data makes every cooking rule
    // (3DS limits, weights, clip baking, material mapping) testable without Assimp or files.

    struct ImportedBoneWeight {
        uint32_t Vertex = 0;
        float Weight = 0.0f;
    };

    struct ImportedBone {
        std::string Name;
        glm::mat4 Offset{ 1.0f }; // mesh space -> bone space (inverse bind pose), column-major
        std::vector<ImportedBoneWeight> Weights;
    };

    struct ImportedMesh {
        std::string Name;
        uint32_t MaterialIndex = 0;
        std::vector<glm::vec3> Positions;
        std::vector<glm::vec3> Normals; // empty = none
        std::vector<glm::vec2> UVs;     // empty = none
        std::vector<glm::vec4> Colors;  // empty = white
        std::vector<uint32_t> Indices;  // triangle list
        std::vector<ImportedBone> Bones;
    };

    struct ImportedNode {
        std::string Name;
        int Parent = -1;
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };
        std::vector<uint32_t> Meshes;
        std::vector<int> Children;
    };

    // A texture slot of a material: an embedded texture index or a file path (as written in
    // the model, usually relative to it).
    struct ImportedTextureRef {
        int Embedded = -1;
        std::string Path;
        bool IsSet() const { return Embedded >= 0 || !Path.empty(); }
    };

    enum class ImportedAlphaMode { Opaque, Mask, Blend };

    struct ImportedMaterial {
        std::string Name;
        glm::vec4 BaseColor{ 1.0f };
        glm::vec3 Emissive{ 0.0f };
        glm::vec3 Specular{ 0.0f };
        float Shininess = 0.0f; // Phong exponent from the file (0 = none)
        float Opacity = 1.0f;
        ImportedAlphaMode AlphaMode = ImportedAlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool TwoSided = false;
        bool Unlit = false; // glTF KHR_materials_unlit / shading model "NoShading"
        ImportedTextureRef BaseColorTexture;
        ImportedTextureRef NormalTexture;
        ImportedTextureRef EmissiveTexture;
    };

    struct ImportedTexture {
        std::string Name;          // file name hint ("" for anonymous embedded data)
        std::vector<uint8_t> Data; // compressed file bytes (png/jpg/...) when Width == 0
        int Width = 0;             // > 0: Data is raw RGBA8, Width x Height
        int Height = 0;
    };

    struct ImportedVec3Key { double Time = 0.0; glm::vec3 Value{ 0.0f }; };
    struct ImportedQuatKey { double Time = 0.0; glm::quat Value{ 1.0f, 0.0f, 0.0f, 0.0f }; };

    struct ImportedChannel {
        std::string Node;
        std::vector<ImportedVec3Key> Positions; // seconds
        std::vector<ImportedQuatKey> Rotations;
        std::vector<ImportedVec3Key> Scales;
    };

    struct ImportedAnimation {
        std::string Name;
        double Duration = 0.0; // seconds
        std::vector<ImportedChannel> Channels;
    };

    struct ImportedModel {
        std::vector<ImportedNode> Nodes; // [0] = root
        std::vector<ImportedMesh> Meshes;
        std::vector<ImportedMaterial> Materials;
        std::vector<ImportedTexture> Textures; // embedded
        std::vector<ImportedAnimation> Animations;
        // Metres per file unit (FBX UnitScaleFactor cm -> 0.01); 1 when the file has no unit.
        float FileUnitScale = 1.0f;
    };

}
