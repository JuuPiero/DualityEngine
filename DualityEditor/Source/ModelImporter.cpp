#include "DualityEditor/ModelImporter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>

#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"

using json = nlohmann::json;

namespace Duality {
    namespace {
        struct CacheEntry {
            std::filesystem::file_time_type Modified{};
            uintmax_t Size = 0;
            ModelImportInfo Info;
        };
        // Content Browser keeps the returned ModelImportInfo reference for the rest of the
        // current ImGui item. A plain unordered_map value can move on a later cache insertion
        // (rehash), leaving that UI code with a dangling reference. Heap-stable entries keep
        // the public reference contract valid while arbitrary model cards are expanded/clicked.
        std::unordered_map<std::string, std::unique_ptr<CacheEntry>> s_Cache;

        std::string LowerExtension(const std::filesystem::path& path) {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return extension;
        }

        std::string SafeName(const aiString& source, const char* fallback, unsigned int index) {
            if (source.length > 0)
                return source.C_Str();
            return std::string(fallback) + " " + std::to_string(index);
        }

        std::filesystem::path ImportDirectory(const std::filesystem::path& sourcePath) {
            const std::string sourceGuid = AssetMeta::EnsureMetaFile(sourcePath);
            if (sourceGuid.empty())
                return {};
            return sourcePath.parent_path() / ".duality-import" / sourceGuid;
        }

        bool WriteMesh(const std::filesystem::path& path, const aiMesh& mesh, float& outRadius) {
            // PICA exposes 96 float-vector uniforms. The engine's forward shader reserves 11,
            // leaving an intentionally conservative 24 * 3-vector affine bone palette. Rather
            // than remove bones, partition triangles offline and remap each draw's indices.
            constexpr uint32_t MaxBonesPerPalette = 24;
            struct Influence { uint32_t Bone = 0; float Weight = 0.0f; };
            struct Partition { std::vector<uint32_t> Palette; std::vector<MeshVertex> Vertices; };
            std::vector<std::vector<Influence>> influences(mesh.mNumVertices);
            if (mesh.mNumBones > 65535)
                return false; // .dmesh V3 palette entries are u16; this is a malformed practical rig.
            for (unsigned int boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex) {
                const aiBone* bone = mesh.mBones[boneIndex];
                for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                    const aiVertexWeight& weight = bone->mWeights[weightIndex];
                    if (weight.mVertexId < influences.size() && weight.mWeight > 0.0f)
                        influences[weight.mVertexId].push_back({ boneIndex, weight.mWeight });
                }
            }
            for (auto& vertexInfluences : influences) {
                std::sort(vertexInfluences.begin(), vertexInfluences.end(), [](const Influence& a, const Influence& b) { return a.Weight > b.Weight; });
                if (vertexInfluences.size() > 4)
                    vertexInfluences.resize(4); // fixed cross-platform vertex contract, then renormalized below.
            }

            auto appendUnique = [](std::vector<uint32_t>& target, uint32_t value) {
                if (std::find(target.begin(), target.end(), value) == target.end()) target.push_back(value);
            };
            auto makeVertex = [&](unsigned int index, const std::vector<uint32_t>& palette) {
                const aiVector3D& p = mesh.mVertices[index];
                const aiVector3D uv = mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][index] : aiVector3D{};
                const aiVector3D n = mesh.HasNormals() ? mesh.mNormals[index] : aiVector3D(0.0f, 1.0f, 0.0f);
                const aiColor4D c = mesh.HasVertexColors(0) ? mesh.mColors[0][index] : aiColor4D(1, 1, 1, 1);
                MeshVertex vertex{ { p.x, p.y, p.z }, { uv.x, uv.y }, { n.x, n.y, n.z }, { c.r, c.g, c.b, c.a } };
                float sum = 0.0f;
                const auto& sourceInfluences = influences[index];
                for (size_t influence = 0; influence < sourceInfluences.size(); ++influence) {
                    const auto found = std::find(palette.begin(), palette.end(), sourceInfluences[influence].Bone);
                    if (found == palette.end()) continue; // cannot occur: the triangle selected this palette.
                    vertex.BoneIndices[static_cast<int>(influence)] = static_cast<uint8_t>(std::distance(palette.begin(), found));
                    vertex.BoneWeights[static_cast<int>(influence)] = sourceInfluences[influence].Weight;
                    sum += sourceInfluences[influence].Weight;
                }
                if (sum > 0.0f) vertex.BoneWeights /= sum;
                return vertex;
            };

            std::vector<Partition> partitions;
            float radiusSq = 0.0f;
            for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex) {
                const aiFace& face = mesh.mFaces[faceIndex];
                if (face.mNumIndices != 3) continue;
                std::vector<uint32_t> triangleBones;
                for (unsigned int corner = 0; corner < 3; ++corner) {
                    const unsigned int index = face.mIndices[corner];
                    if (index >= mesh.mNumVertices) return false;
                    for (const Influence& influence : influences[index]) appendUnique(triangleBones, influence.Bone);
                }
                // Best-fit greedy bin packing: prefer the palette requiring the fewest new
                // bones, then the newest matching partition for triangle locality. A triangle
                // has at most 12 retained bones, so it always fits an empty 24-bone palette.
                size_t selected = partitions.size();
                size_t bestNewBones = MaxBonesPerPalette + 1;
                for (size_t partitionIndex = 0; partitionIndex < partitions.size(); ++partitionIndex) {
                    const auto& palette = partitions[partitionIndex].Palette;
                    size_t additions = 0;
                    for (uint32_t bone : triangleBones)
                        if (std::find(palette.begin(), palette.end(), bone) == palette.end()) ++additions;
                    if (palette.size() + additions <= MaxBonesPerPalette && additions <= bestNewBones) {
                        selected = partitionIndex;
                        bestNewBones = additions;
                    }
                }
                if (selected == partitions.size()) partitions.emplace_back();
                Partition& partition = partitions[selected];
                for (uint32_t bone : triangleBones) appendUnique(partition.Palette, bone);
                for (unsigned int corner = 0; corner < 3; ++corner) {
                    const unsigned int index = face.mIndices[corner];
                    partition.Vertices.push_back(makeVertex(index, partition.Palette));
                    const aiVector3D& p = mesh.mVertices[index];
                    radiusSq = std::max(radiusSq, p.x * p.x + p.y * p.y + p.z * p.z);
                }
            }
            size_t vertexTotal = 0;
            for (const Partition& partition : partitions) vertexTotal += partition.Vertices.size();
            if (vertexTotal == 0) return false;
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
                return false;
            const char magic[] = { 'D', 'M', 'S', 'H' };
            const uint32_t version = 3, vertexCount = static_cast<uint32_t>(vertexTotal), subMeshCount = static_cast<uint32_t>(partitions.size()), boneCount = mesh.mNumBones;
            outRadius = std::sqrt(radiusSq);
            output.write(magic, sizeof(magic));
            output.write(reinterpret_cast<const char*>(&version), sizeof(version));
            output.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
            output.write(reinterpret_cast<const char*>(&subMeshCount), sizeof(subMeshCount));
            output.write(reinterpret_cast<const char*>(&outRadius), sizeof(outRadius));
            output.write(reinterpret_cast<const char*>(&boneCount), sizeof(boneCount));
            for (unsigned int boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex) {
                const aiBone* bone = mesh.mBones[boneIndex];
                const uint32_t nameLength = bone->mName.length;
                output.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
                output.write(bone->mName.C_Str(), nameLength);
                // aiMatrix4x4 and glm::mat4 are both 16 scalar floats, but their row/column
                // memory order differs. Transpose while writing so runtime GLM reads the same transform.
                float offset[16];
                for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
                    offset[column * 4 + row] = bone->mOffsetMatrix[row][column];
                output.write(reinterpret_cast<const char*>(offset), sizeof(offset));
            }
            for (const Partition& partition : partitions) for (const MeshVertex& v : partition.Vertices) {
                const float values[] = { v.Position.x, v.Position.y, v.Position.z, v.TexCoord.x, v.TexCoord.y,
                    v.Normal.x, v.Normal.y, v.Normal.z, v.Color.r, v.Color.g, v.Color.b, v.Color.a };
                output.write(reinterpret_cast<const char*>(values), sizeof(values));
                output.write(reinterpret_cast<const char*>(&v.BoneIndices), sizeof(uint8_t) * 4);
                output.write(reinterpret_cast<const char*>(&v.BoneWeights), sizeof(float) * 4);
            }
            uint32_t firstVertex = 0;
            for (const Partition& partition : partitions) {
                const uint32_t count = static_cast<uint32_t>(partition.Vertices.size());
                const uint32_t paletteCount = static_cast<uint32_t>(partition.Palette.size());
                output.write(reinterpret_cast<const char*>(&firstVertex), sizeof(firstVertex));
                output.write(reinterpret_cast<const char*>(&count), sizeof(count));
                output.write(reinterpret_cast<const char*>(&paletteCount), sizeof(paletteCount));
                for (uint32_t bone : partition.Palette) {
                    const uint16_t compactBone = static_cast<uint16_t>(bone);
                    output.write(reinterpret_cast<const char*>(&compactBone), sizeof(compactBone));
                }
                firstVertex += count;
            }
            return static_cast<bool>(output);
        }

        ModelSubAssetInfo CookMeshChild(const aiMesh& mesh, unsigned int index, const std::filesystem::path& directory) {
            ModelSubAssetInfo child{ "Mesh", SafeName(mesh.mName, "Mesh", index),
                std::to_string(mesh.mNumVertices) + " vertices, " + std::to_string(mesh.mNumFaces) + " triangles, " +
                std::to_string(mesh.mNumBones) + " bones", index };
            if (directory.empty()) return child;
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            const std::filesystem::path output = directory / ("mesh_" + std::to_string(index) + ".dmesh");
            float radius = 0.0f;
            if (!error && WriteMesh(output, mesh, radius)) {
                child.Detail += ", cooked .dmesh";
                child.AssetPath = output.string();
                child.AssetGuid = AssetMeta::EnsureMetaFile(output);
                AssetDatabase::Register(child.AssetGuid, child.AssetPath);
            } else {
                child.Detail += ", cook failed";
            }
            return child;
        }

        ModelSubAssetInfo CookAnimationChild(const aiAnimation& animation, unsigned int index, const std::filesystem::path& directory) {
            const double ticks = animation.mTicksPerSecond > 0.0 ? animation.mTicksPerSecond : 25.0;
            const double seconds = animation.mDuration / ticks;
            ModelSubAssetInfo child{ "Animation", SafeName(animation.mName, "Animation", index),
                std::to_string(animation.mNumChannels) + " channels, " + std::to_string(seconds) + " sec", index };
            if (directory.empty()) return child;
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            const std::filesystem::path output = directory / ("animation_" + std::to_string(index) + ".anim");
            json root = { { "Version", 1 }, { "Name", child.Name }, { "Duration", seconds }, { "Channels", json::array() } };
            for (unsigned int channelIndex = 0; channelIndex < animation.mNumChannels; ++channelIndex) {
                const aiNodeAnim* channel = animation.mChannels[channelIndex];
                json result = { { "Node", channel->mNodeName.C_Str() }, { "Position", json::array() }, { "Rotation", json::array() }, { "Scale", json::array() } };
                for (unsigned int key = 0; key < channel->mNumPositionKeys; ++key) {
                    const aiVectorKey& k = channel->mPositionKeys[key];
                    result["Position"].push_back({ k.mTime / ticks, { k.mValue.x, k.mValue.y, k.mValue.z } });
                }
                for (unsigned int key = 0; key < channel->mNumRotationKeys; ++key) {
                    const aiQuatKey& k = channel->mRotationKeys[key];
                    result["Rotation"].push_back({ k.mTime / ticks, { k.mValue.x, k.mValue.y, k.mValue.z, k.mValue.w } });
                }
                for (unsigned int key = 0; key < channel->mNumScalingKeys; ++key) {
                    const aiVectorKey& k = channel->mScalingKeys[key];
                    result["Scale"].push_back({ k.mTime / ticks, { k.mValue.x, k.mValue.y, k.mValue.z } });
                }
                root["Channels"].push_back(std::move(result));
            }
            std::ofstream stream(output);
            if (!error && stream) {
                stream << root.dump(2);
                child.AssetPath = output.string();
                child.AssetGuid = AssetMeta::EnsureMetaFile(output);
                AssetDatabase::Register(child.AssetGuid, child.AssetPath);
                child.Detail += ", cooked .anim";
            } else child.Detail += ", cook failed";
            return child;
        }

        void AddTextureChildren(const aiScene& scene, ModelImportInfo& info) {
            for (unsigned int i = 0; i < scene.mNumTextures; ++i) {
                const aiTexture* texture = scene.mTextures[i];
                const std::string name = texture ? SafeName(texture->mFilename, "Embedded Texture", i)
                                                 : "Embedded Texture " + std::to_string(i);
                info.Children.push_back({ "Texture", name, "embedded *" + std::to_string(i), i });
            }

            constexpr aiTextureType types[] = {
                aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, aiTextureType_NORMALS,
                aiTextureType_EMISSIVE, aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS
            };
            for (unsigned int materialIndex = 0; materialIndex < scene.mNumMaterials; ++materialIndex) {
                const aiMaterial* material = scene.mMaterials[materialIndex];
                for (aiTextureType type : types) {
                    const unsigned int count = material->GetTextureCount(type);
                    for (unsigned int textureIndex = 0; textureIndex < count; ++textureIndex) {
                        aiString path;
                        if (material->GetTexture(type, textureIndex, &path) == AI_SUCCESS)
                            info.Children.push_back({ "Texture", path.C_Str(), "material " + std::to_string(materialIndex), textureIndex });
                    }
                }
            }
        }

        void SaveInventory(const std::filesystem::path& path, const ModelImportInfo& info) {
            const std::string guid = AssetMeta::EnsureMetaFile(path);
            if (guid.empty())
                return;
            std::filesystem::path metaPath = path;
            metaPath += ".meta";
            json root = json::object();
            std::ifstream input(metaPath);
            try { if (input) input >> root; } catch (const json::parse_error&) { root = json::object(); }
            root["guid"] = guid;
            json children = json::array();
            for (const ModelSubAssetInfo& child : info.Children)
                children.push_back({ { "Type", child.Type }, { "Name", child.Name }, { "Detail", child.Detail }, { "Index", child.Index },
                    { "AssetGuid", child.AssetGuid }, { "AssetPath", child.AssetPath } });
            root["ModelImporter"] = { { "Version", 1 }, { "Children", children } };
            std::ofstream output(metaPath);
            if (output)
                output << root.dump(2);
        }

        ModelImportInfo ReadModel(const std::filesystem::path& path) {
            ModelImportInfo info;
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(path.string(),
                aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
                aiProcess_CalcTangentSpace | aiProcess_ImproveCacheLocality | aiProcess_SortByPType |
                aiProcess_ValidateDataStructure);
            if (!scene || !scene->mRootNode) {
                info.Error = importer.GetErrorString();
                return info;
            }
            info.Valid = true;
            const std::filesystem::path outputDirectory = ImportDirectory(path);
            for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
                const aiMesh* mesh = scene->mMeshes[i];
                info.Children.push_back(CookMeshChild(*mesh, i, outputDirectory));
            }
            for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
                aiString name;
                scene->mMaterials[i]->Get(AI_MATKEY_NAME, name);
                info.Children.push_back({ "Material", SafeName(name, "Material", i), "source material", i });
            }
            AddTextureChildren(*scene, info);
            for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
                const aiAnimation* animation = scene->mAnimations[i];
                info.Children.push_back(CookAnimationChild(*animation, i, outputDirectory));
            }
            SaveInventory(path, info);
            return info;
        }
    }

    bool ModelImporter::IsSupported(const std::filesystem::path& path) {
        const std::string extension = LowerExtension(path);
        return extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb";
    }

    const ModelImportInfo& ModelImporter::Inspect(const std::filesystem::path& path) {
        static const ModelImportInfo unsupported{};
        if (!IsSupported(path))
            return unsupported;
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(path, error);
        const uintmax_t size = error ? 0 : std::filesystem::file_size(path, error);
        const std::string key = path.lexically_normal().string();
        auto found = s_Cache.find(key);
        if (found != s_Cache.end() && found->second->Modified == modified && found->second->Size == size)
            return found->second->Info;
        if (found == s_Cache.end())
            found = s_Cache.emplace(key, std::make_unique<CacheEntry>()).first;
        CacheEntry& entry = *found->second;
        entry.Modified = modified;
        entry.Size = size;
        entry.Info = ReadModel(path);
        if (!entry.Info.Valid)
            Log::Warn("ModelImporter: could not inspect '" + path.string() + "': " + entry.Info.Error);
        return entry.Info;
    }

    void ModelImporter::Invalidate(const std::filesystem::path& path) {
        s_Cache.erase(path.lexically_normal().string());
    }
}
