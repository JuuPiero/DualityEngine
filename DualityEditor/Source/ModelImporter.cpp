#include "DualityEditor/ModelImporter.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/ProgressHandler.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>

#include "DualityEditor/Import/ImportedModel.h"
#include "DualityEditor/Import/ModelCooker.h"
#include "DualityEditor/Import/ModelImportSettings.h"
#include "DualityEditor/Import/ModelPrefabBuilder.h"
#include "DualityEditor/Import/TextureBaker.h"
#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/ModelAsset.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Core/Log.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Duality {

    namespace {
        constexpr int InventoryVersion = 2;

        std::string LowerExtension(const fs::path& path) {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return extension;
        }

        std::string KeyOf(const fs::path& path) {
            std::error_code error;
            const fs::path absolute = fs::absolute(path, error);
            return (error ? path : absolute).lexically_normal().generic_string();
        }

        int64_t ModifiedTicks(const fs::path& path, uintmax_t* size = nullptr) {
            std::error_code error;
            const auto modified = fs::last_write_time(path, error);
            if (size) {
                std::error_code sizeError;
                *size = fs::file_size(path, sizeError);
                if (sizeError) *size = 0;
            }
            return error ? 0 : static_cast<int64_t>(modified.time_since_epoch().count());
        }

        std::string HashFile(const fs::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return {};
            uint64_t hash = 1469598103934665603ull;
            std::vector<char> buffer(1 << 16);
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize read = input.gcount();
                if (read > 0) hash = Fnv1a64(buffer.data(), static_cast<size_t>(read), hash);
            }
            return HexString(hash);
        }

        json ReadJsonFile(const fs::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return json::object();
            std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
                static_cast<unsigned char>(text[2]) == 0xBF)
                text.erase(0, 3);
            json root = json::parse(text, nullptr, false);
            return root.is_object() ? root : json::object();
        }

        bool WriteMeta(const fs::path& assetPath, const std::string& guid) {
            fs::path metaPath = assetPath;
            metaPath += ".meta";
            json root = ReadJsonFile(metaPath);
            if (root.value("guid", std::string()) == guid)
                return true;
            root["guid"] = guid;
            return ModelCooker::WriteTextAtomic(metaPath.string(), root.dump(4));
        }

        std::string SanitizeFileName(std::string name) {
            for (char& c : name)
                if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 32)
                    c = '_';
            while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
            return name.empty() ? std::string("Asset") : name;
        }

        // ---- Budget (de)serialization ---------------------------------------------------------
        json BudgetToJson(const ModelBudget& budget) {
            json meshes = json::array(), textures = json::array();
            for (const ModelBudgetMesh& mesh : budget.Meshes)
                meshes.push_back({ { "Name", mesh.Name }, { "Vertices", mesh.Vertices }, { "Triangles", mesh.Triangles },
                    { "RuntimeVertices", mesh.RuntimeVertices }, { "RuntimeBytes", mesh.RuntimeBytes }, { "FileBytes", mesh.FileBytes },
                    { "SubMeshes", mesh.SubMeshes }, { "Bones", mesh.Bones }, { "MaxPaletteBones", mesh.MaxPaletteBones },
                    { "ClampedInfluences", mesh.ClampedInfluences } });
            for (const ModelBudgetTexture& texture : budget.Textures)
                textures.push_back({ { "Name", texture.Name }, { "SourceWidth", texture.SourceWidth }, { "SourceHeight", texture.SourceHeight },
                    { "Width", texture.Width }, { "Height", texture.Height }, { "Format", texture.Format }, { "Bytes", texture.Bytes } });
            return { { "Meshes", meshes }, { "Textures", textures }, { "Triangles", budget.Triangles },
                { "RuntimeVertexBytes", budget.RuntimeVertexBytes }, { "TextureBytes", budget.TextureBytes },
                { "DrawCalls", budget.DrawCalls }, { "Bones", budget.Bones }, { "Clips", budget.Clips }, { "ClipKeys", budget.ClipKeys },
                { "Warnings", budget.Warnings } };
        }

        ModelBudget BudgetFromJson(const json& value) {
            ModelBudget budget;
            if (!value.is_object())
                return budget;
            for (const json& mesh : value.value("Meshes", json::array()))
                budget.Meshes.push_back({ mesh.value("Name", std::string()), mesh.value("Vertices", 0u), mesh.value("Triangles", 0u),
                    mesh.value("RuntimeVertices", 0u), mesh.value("RuntimeBytes", 0u), mesh.value("FileBytes", 0u),
                    mesh.value("SubMeshes", 0u), mesh.value("Bones", 0u), mesh.value("MaxPaletteBones", 0u), mesh.value("ClampedInfluences", 0u) });
            for (const json& texture : value.value("Textures", json::array()))
                budget.Textures.push_back({ texture.value("Name", std::string()), texture.value("SourceWidth", 0), texture.value("SourceHeight", 0),
                    texture.value("Width", 0), texture.value("Height", 0), texture.value("Format", std::string("Auto")), texture.value("Bytes", 0u) });
            budget.Triangles = value.value("Triangles", 0u);
            budget.RuntimeVertexBytes = value.value("RuntimeVertexBytes", 0u);
            budget.TextureBytes = value.value("TextureBytes", 0u);
            budget.DrawCalls = value.value("DrawCalls", 0u);
            budget.Bones = value.value("Bones", 0u);
            budget.Clips = value.value("Clips", 0u);
            budget.ClipKeys = value.value("ClipKeys", 0u);
            for (const json& warning : value.value("Warnings", json::array()))
                if (warning.is_string()) budget.Warnings.push_back(warning.get<std::string>());
            return budget;
        }

        // ---- Jobs -------------------------------------------------------------------------------
        struct Job {
            enum class Kind { Import, Scan } Type = Kind::Import;
            std::string Key;        // normalized model path
            fs::path ModelPath;
            std::string ModelGuid;
            ModelImportSettings Settings;
            json Previous;          // previous "ModelImporter" inventory block
            bool Force = false;
            fs::path ScanRoot;
        };

        struct ScannedModel {
            std::string Key;
            fs::path Path;
            uintmax_t Size = 0;
            int64_t Modified = 0;
        };

        struct JobResult {
            Job::Kind Type = Job::Kind::Import;
            std::string Key;
            fs::path ModelPath;
            std::string ModelGuid;
            bool Success = false;
            bool UpToDate = false;
            std::string Error;
            std::string SourceHash;
            uintmax_t SourceSize = 0;
            int64_t SourceModified = 0;
            ModelImportInfo Info;
            ModelPrefabPlan Prefab;
            std::string PrefabGuid;
            std::string SettingsHash;
            std::vector<std::pair<std::string, std::string>> Registrations; // guid, path
            std::vector<std::pair<int, std::string>> Logs;                  // 0 info, 1 warn, 2 error
            std::vector<ScannedModel> Scanned;
        };

        struct ExternalRequest {
            std::string Key;
            fs::path Path;
        };

        struct Entry {
            fs::path Path;
            ModelImportInfo Info;
            ModelImportState State = ModelImportState::Unknown;
            bool Evaluated = false;
            bool Queued = false;
            uintmax_t ObservedSize = 0;
            int64_t ObservedModified = 0;
        };

        class AbortHandler : public Assimp::ProgressHandler {
        public:
            explicit AbortHandler(std::atomic<float>& progress, std::atomic<bool>& stop) : m_Progress(progress), m_Stop(stop) {}
            bool Update(float percentage) override {
                if (percentage >= 0.0f) m_Progress = 0.05f + 0.35f * std::clamp(percentage, 0.0f, 1.0f);
                return !m_Stop.load();
            }
        private:
            std::atomic<float>& m_Progress;
            std::atomic<bool>& m_Stop;
        };

        // ------------------------------------------------------------------------------------
        // Worker: Assimp -> ImportedModel -> cooked files. Never touches AssetDatabase, the Log
        // or the scene; everything the main thread must do is returned in the JobResult.
        // ------------------------------------------------------------------------------------
        glm::mat4 ToGlm(const aiMatrix4x4& m) {
            glm::mat4 result(1.0f);
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    result[column][row] = m[row][column];
            return result;
        }

        ImportedTextureRef TextureRefOf(const aiScene& scene, const aiMaterial& material, std::initializer_list<aiTextureType> types) {
            ImportedTextureRef ref;
            for (aiTextureType type : types) {
                if (material.GetTextureCount(type) == 0)
                    continue;
                aiString path;
                if (material.GetTexture(type, 0, &path) != AI_SUCCESS || path.length == 0)
                    continue;
                const auto embedded = scene.GetEmbeddedTextureAndIndex(path.C_Str());
                if (embedded.first && embedded.second >= 0)
                    ref.Embedded = embedded.second;
                else
                    ref.Path = path.C_Str();
                return ref;
            }
            return ref;
        }

        void ConvertNode(const aiNode& node, int parent, ImportedModel& model) {
            const int index = static_cast<int>(model.Nodes.size());
            model.Nodes.emplace_back();
            ImportedNode& out = model.Nodes.back();
            out.Name = node.mName.C_Str();
            out.Parent = parent;
            aiVector3D scaling, position;
            aiQuaternion rotation;
            node.mTransformation.Decompose(scaling, rotation, position);
            out.Translation = { position.x, position.y, position.z };
            out.Rotation = glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
            out.Scale = { scaling.x, scaling.y, scaling.z };
            for (unsigned int i = 0; i < node.mNumMeshes; ++i)
                out.Meshes.push_back(node.mMeshes[i]);
            if (parent >= 0)
                model.Nodes[static_cast<size_t>(parent)].Children.push_back(index);
            for (unsigned int i = 0; i < node.mNumChildren; ++i)
                ConvertNode(*node.mChildren[i], index, model);
        }

        ImportedModel Convert(const aiScene& scene, const ModelImportSettings& settings) {
            ImportedModel model;
            if (scene.mRootNode)
                ConvertNode(*scene.mRootNode, -1, model);
            if (scene.mMetaData) {
                double unit = 0.0;
                float unitFloat = 0.0f;
                if (scene.mMetaData->Get("UnitScaleFactor", unit) && unit > 0.0)
                    model.FileUnitScale = static_cast<float>(unit * 0.01); // FBX: 1.0 = centimetre
                else if (scene.mMetaData->Get("UnitScaleFactor", unitFloat) && unitFloat > 0.0f)
                    model.FileUnitScale = unitFloat * 0.01f;
            }
            for (unsigned int i = 0; i < scene.mNumMeshes; ++i) {
                const aiMesh& source = *scene.mMeshes[i];
                ImportedMesh mesh;
                mesh.Name = source.mName.C_Str();
                mesh.MaterialIndex = source.mMaterialIndex;
                mesh.Positions.reserve(source.mNumVertices);
                for (unsigned int v = 0; v < source.mNumVertices; ++v)
                    mesh.Positions.push_back({ source.mVertices[v].x, source.mVertices[v].y, source.mVertices[v].z });
                if (source.HasNormals() && settings.Normals != ModelNormalsMode::None)
                    for (unsigned int v = 0; v < source.mNumVertices; ++v)
                        mesh.Normals.push_back({ source.mNormals[v].x, source.mNormals[v].y, source.mNormals[v].z });
                if (source.HasTextureCoords(0))
                    for (unsigned int v = 0; v < source.mNumVertices; ++v)
                        mesh.UVs.push_back({ source.mTextureCoords[0][v].x, source.mTextureCoords[0][v].y });
                if (source.HasVertexColors(0) && settings.ImportVertexColors)
                    for (unsigned int v = 0; v < source.mNumVertices; ++v) {
                        const aiColor4D& c = source.mColors[0][v];
                        mesh.Colors.push_back({ c.r, c.g, c.b, c.a });
                    }
                for (unsigned int f = 0; f < source.mNumFaces; ++f) {
                    const aiFace& face = source.mFaces[f];
                    if (face.mNumIndices != 3) continue;
                    mesh.Indices.insert(mesh.Indices.end(), { face.mIndices[0], face.mIndices[1], face.mIndices[2] });
                }
                for (unsigned int b = 0; b < source.mNumBones; ++b) {
                    const aiBone& bone = *source.mBones[b];
                    ImportedBone out;
                    out.Name = bone.mName.C_Str();
                    out.Offset = ToGlm(bone.mOffsetMatrix);
                    out.Weights.reserve(bone.mNumWeights);
                    for (unsigned int w = 0; w < bone.mNumWeights; ++w)
                        out.Weights.push_back({ bone.mWeights[w].mVertexId, bone.mWeights[w].mWeight });
                    mesh.Bones.push_back(std::move(out));
                }
                model.Meshes.push_back(std::move(mesh));
            }
            for (unsigned int i = 0; i < scene.mNumMaterials; ++i) {
                const aiMaterial& source = *scene.mMaterials[i];
                ImportedMaterial material;
                aiString name;
                if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) material.Name = name.C_Str();
                if (material.Name.empty()) material.Name = "Material " + std::to_string(i);
                aiColor4D color;
                if (source.Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS || source.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
                    material.BaseColor = { color.r, color.g, color.b, color.a };
                aiColor3D emissive, specular;
                if (source.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) material.Emissive = { emissive.r, emissive.g, emissive.b };
                float intensity = 1.0f;
                if (source.Get(AI_MATKEY_EMISSIVE_INTENSITY, intensity) == AI_SUCCESS) material.Emissive *= intensity;
                if (source.Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS) material.Specular = { specular.r, specular.g, specular.b };
                source.Get(AI_MATKEY_SHININESS, material.Shininess);
                source.Get(AI_MATKEY_OPACITY, material.Opacity);
                int twoSided = 0;
                if (source.Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) material.TwoSided = twoSided != 0;
                int shading = 0;
                if (source.Get(AI_MATKEY_SHADING_MODEL, shading) == AI_SUCCESS)
                    material.Unlit = shading == aiShadingMode_NoShading || shading == aiShadingMode_Unlit;
                aiString alphaMode;
                if (source.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
                    const std::string mode = alphaMode.C_Str();
                    material.AlphaMode = mode == "BLEND" ? ImportedAlphaMode::Blend : (mode == "MASK" ? ImportedAlphaMode::Mask : ImportedAlphaMode::Opaque);
                }
                source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, material.AlphaCutoff);
                material.BaseColorTexture = TextureRefOf(scene, source, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
                material.NormalTexture = TextureRefOf(scene, source, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA });
                material.EmissiveTexture = TextureRefOf(scene, source, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR });
                model.Materials.push_back(std::move(material));
            }
            for (unsigned int i = 0; i < scene.mNumTextures; ++i) {
                const aiTexture& source = *scene.mTextures[i];
                ImportedTexture texture;
                texture.Name = source.mFilename.C_Str();
                if (source.mHeight == 0) {
                    const auto* bytes = reinterpret_cast<const uint8_t*>(source.pcData);
                    texture.Data.assign(bytes, bytes + source.mWidth);
                } else {
                    texture.Width = static_cast<int>(source.mWidth);
                    texture.Height = static_cast<int>(source.mHeight);
                    texture.Data.resize(static_cast<size_t>(texture.Width) * texture.Height * 4);
                    for (size_t p = 0; p < static_cast<size_t>(texture.Width) * texture.Height; ++p) {
                        const aiTexel& texel = source.pcData[p];
                        texture.Data[p * 4 + 0] = texel.r; texture.Data[p * 4 + 1] = texel.g;
                        texture.Data[p * 4 + 2] = texel.b; texture.Data[p * 4 + 3] = texel.a;
                    }
                }
                model.Textures.push_back(std::move(texture));
            }
            for (unsigned int i = 0; i < scene.mNumAnimations; ++i) {
                const aiAnimation& source = *scene.mAnimations[i];
                const double ticks = source.mTicksPerSecond > 0.0 ? source.mTicksPerSecond : 25.0;
                ImportedAnimation animation;
                animation.Name = source.mName.length ? source.mName.C_Str() : "Take " + std::to_string(i);
                animation.Duration = source.mDuration / ticks;
                for (unsigned int c = 0; c < source.mNumChannels; ++c) {
                    const aiNodeAnim& channel = *source.mChannels[c];
                    ImportedChannel out;
                    out.Node = channel.mNodeName.C_Str();
                    for (unsigned int k = 0; k < channel.mNumPositionKeys; ++k) {
                        const aiVectorKey& key = channel.mPositionKeys[k];
                        out.Positions.push_back({ key.mTime / ticks, { key.mValue.x, key.mValue.y, key.mValue.z } });
                    }
                    for (unsigned int k = 0; k < channel.mNumRotationKeys; ++k) {
                        const aiQuatKey& key = channel.mRotationKeys[k];
                        out.Rotations.push_back({ key.mTime / ticks, glm::normalize(glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z)) });
                    }
                    for (unsigned int k = 0; k < channel.mNumScalingKeys; ++k) {
                        const aiVectorKey& key = channel.mScalingKeys[k];
                        out.Scales.push_back({ key.mTime / ticks, { key.mValue.x, key.mValue.y, key.mValue.z } });
                    }
                    animation.Channels.push_back(std::move(out));
                }
                model.Animations.push_back(std::move(animation));
            }
            return model;
        }

        std::string NodePath(const ImportedModel& model, int index) {
            std::string path;
            for (int current = index; current > 0; current = model.Nodes[static_cast<size_t>(current)].Parent)
                path = model.Nodes[static_cast<size_t>(current)].Name + (path.empty() ? "" : "/" + path);
            return path;
        }

        // Previous inventory: Key -> {guid, file}. V1 inventories had no keys: their
        // mesh/animation GUIDs are matched by (type, index) so references made with the first
        // importer keep working after the upgrade.
        struct PreviousChild { std::string Guid; std::string File; };
        struct PreviousInventory {
            std::unordered_map<std::string, PreviousChild> ByKey;
            std::map<std::pair<std::string, unsigned>, PreviousChild> LegacyByIndex;
            std::string PrefabGuid;
        };

        PreviousInventory ReadPrevious(const json& block) {
            PreviousInventory previous;
            if (!block.is_object())
                return previous;
            const int version = block.value("Version", 1);
            for (const json& child : block.value("Children", json::array())) {
                if (!child.is_object()) continue;
                PreviousChild entry{ child.value("AssetGuid", std::string()), child.value("File", std::string()) };
                if (entry.Guid.empty()) continue;
                if (entry.File.empty()) {
                    const std::string assetPath = child.value("AssetPath", std::string());
                    if (!assetPath.empty()) entry.File = fs::path(assetPath).filename().string();
                }
                if (version >= 2 && child.contains("Key"))
                    previous.ByKey[child.value("Key", std::string())] = entry;
                else
                    previous.LegacyByIndex[{ child.value("Type", std::string()), child.value("Index", 0u) }] = entry;
            }
            if (block.contains("Prefab") && block["Prefab"].is_object())
                previous.PrefabGuid = block["Prefab"].value("Guid", std::string());
            return previous;
        }

        bool OutputsExist(const json& block, const fs::path& importDirectory) {
            if (!block.is_object())
                return false;
            for (const json& child : block.value("Children", json::array())) {
                const std::string file = child.value("File", std::string());
                if (!child.value("AssetGuid", std::string()).empty() && child.value("Generated", false) && !file.empty()) {
                    std::error_code error;
                    if (!fs::is_regular_file(importDirectory / file, error)) return false;
                }
            }
            if (block.contains("Prefab") && block["Prefab"].is_object()) {
                std::error_code error;
                if (!fs::is_regular_file(importDirectory / block["Prefab"].value("File", std::string("model.prefab")), error)) return false;
            }
            return true;
        }

        struct WorkerContext {
            std::atomic<float>* Progress = nullptr;
            std::atomic<bool>* Stop = nullptr;
        };

        void RunImport(const Job& job, JobResult& result, WorkerContext& context) {
            auto progress = [&](float value) { if (context.Progress) *context.Progress = value; };
            auto log = [&](int level, const std::string& message) { result.Logs.push_back({ level, message }); };
            result.Type = Job::Kind::Import;
            result.Key = job.Key;
            result.ModelPath = job.ModelPath;
            result.ModelGuid = job.ModelGuid;
            result.SettingsHash = job.Settings.Hash();
            result.SourceModified = ModifiedTicks(job.ModelPath, &result.SourceSize);
            progress(0.02f);
            result.SourceHash = HashFile(job.ModelPath);
            if (result.SourceHash.empty()) {
                result.Error = "cannot read the file";
                return;
            }
            const fs::path importDirectory = ModelAsset::ImportDirectory(job.ModelPath.string(), job.ModelGuid);
            const json& previousBlock = job.Previous;
            if (!job.Force && previousBlock.is_object() && previousBlock.value("Version", 1) == InventoryVersion &&
                previousBlock.value("ImporterVersion", 0) == ModelImporter::ImporterVersion &&
                previousBlock.value("SettingsHash", std::string()) == result.SettingsHash &&
                previousBlock.contains("Source") && previousBlock["Source"].value("Hash", std::string()) == result.SourceHash &&
                OutputsExist(previousBlock, importDirectory)) {
                result.Success = true;
                result.UpToDate = true;
                return;
            }

            const ModelImportSettings& settings = job.Settings;
            Assimp::Importer importer;
            importer.SetProgressHandler(new AbortHandler(*context.Progress, *context.Stop)); // owned by the importer
            unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                aiProcess_ValidateDataStructure | aiProcess_LimitBoneWeights;
            int removeComponents = 0;
            if (settings.Normals == ModelNormalsMode::Calculate) {
                removeComponents |= aiComponent_NORMALS;
                flags |= aiProcess_GenSmoothNormals;
                importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, settings.SmoothingAngle);
            } else if (settings.Normals == ModelNormalsMode::Import) {
                flags |= aiProcess_GenSmoothNormals; // only fills meshes that have none
            } else {
                removeComponents |= aiComponent_NORMALS;
            }
            if (!settings.ImportVertexColors) removeComponents |= aiComponent_COLORS;
            if (removeComponents) {
                flags |= aiProcess_RemoveComponent;
                importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, removeComponents);
            }
            if (settings.Tangents != ModelTangentsMode::None) flags |= aiProcess_CalcTangentSpace;
            if (settings.OptimizeMesh) flags |= aiProcess_ImproveCacheLocality;
            importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, settings.MaxBonesPerVertex);
            importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
            importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
            progress(0.05f);
            const aiScene* scene = importer.ReadFile(job.ModelPath.string(), flags);
            if (!scene || !scene->mRootNode) {
                result.Error = importer.GetErrorString();
                if (result.Error.empty()) result.Error = "Assimp could not read the file";
                return;
            }
            ImportedModel model = Convert(*scene, settings);
            importer.FreeScene();
            progress(0.45f);

            const PreviousInventory previous = ReadPrevious(previousBlock);
            std::set<std::string> reserved; // GUIDs handed to user assets through remaps
            for (const auto& [name, guid] : settings.MaterialRemaps) reserved.insert(guid);
            for (const auto& [key, guid] : settings.TextureRemaps) reserved.insert(guid);
            std::set<std::string> usedGuids;
            // GUID of a sub-asset: the one it had before (by key; first-importer inventories by
            // type + index), else derived from model GUID + key. File names are plain
            // "<type>_<index>" and may change between imports -- references use GUIDs only.
            auto chooseGuid = [&](const std::string& key, const std::string& legacyType, unsigned legacyIndex) {
                std::string guid;
                if (auto found = previous.ByKey.find(key); found != previous.ByKey.end())
                    guid = found->second.Guid;
                else if (auto legacy = previous.LegacyByIndex.find({ legacyType, legacyIndex }); !legacyType.empty() && legacy != previous.LegacyByIndex.end())
                    guid = legacy->second.Guid;
                if (guid.empty() || reserved.count(guid) || usedGuids.count(guid))
                    guid = ModelCooker::StableGuid(job.ModelGuid, key);
                for (int salt = 1; reserved.count(guid) || usedGuids.count(guid); ++salt)
                    guid = ModelCooker::StableGuid(job.ModelGuid, key + "#" + std::to_string(salt));
                usedGuids.insert(guid);
                return guid;
            };

            std::error_code error;
            fs::create_directories(importDirectory, error);
            std::set<std::string> producedFiles;
            ModelImportInfo& info = result.Info;
            info.Valid = true;
            info.FileUnitScale = model.FileUnitScale;
            const float scale = settings.ScaleFactor * (settings.ConvertUnits ? model.FileUnitScale : 1.0f);
            const bool rig = settings.Rig == ModelRigType::Generic;
            const std::string stem = job.ModelPath.stem().string();
            ModelBudget& budget = info.Budget;
            const ModelCooker::BudgetLimits limits;

            // ---- Textures (referenced by materials) ----------------------------------------------
            std::map<std::string, std::string> textureGuidByKey;
            std::vector<ModelSubAssetInfo> textureChildren;
            auto textureKey = [&](const ImportedTextureRef& ref) {
                if (ref.Embedded >= 0) {
                    const std::string& name = ref.Embedded < static_cast<int>(model.Textures.size()) ? model.Textures[static_cast<size_t>(ref.Embedded)].Name : std::string();
                    return std::string("*") + std::to_string(ref.Embedded) + (name.empty() ? "" : ":" + fs::path(name).filename().string());
                }
                std::string path = ref.Path;
                std::replace(path.begin(), path.end(), '\\', '/');
                return path;
            };
            auto resolveTexture = [&](const ImportedTextureRef& ref) -> std::string {
                if (!ref.IsSet() || settings.MaterialCreation == ModelMaterialCreation::None)
                    return {};
                const std::string key = textureKey(ref);
                if (auto cached = textureGuidByKey.find(key); cached != textureGuidByKey.end())
                    return cached->second;
                if (auto remap = settings.TextureRemaps.find(key); remap != settings.TextureRemaps.end()) {
                    textureGuidByKey[key] = remap->second;
                    textureChildren.push_back({ "Texture", fs::path(key).filename().string(), "remapped", static_cast<unsigned>(textureChildren.size()), remap->second, {}, "texture:" + key });
                    return remap->second;
                }
                TextureBaker::Image image;
                std::string sourceName;
                if (ref.Embedded >= 0 && ref.Embedded < static_cast<int>(model.Textures.size())) {
                    const ImportedTexture& texture = model.Textures[static_cast<size_t>(ref.Embedded)];
                    sourceName = texture.Name.empty() ? "Embedded " + std::to_string(ref.Embedded) : fs::path(texture.Name).filename().string();
                    if (texture.Width > 0) {
                        image.Width = texture.Width; image.Height = texture.Height; image.Rgba = texture.Data;
                    } else if (!TextureBaker::DecodeMemory(texture.Data, image)) {
                        log(1, "ModelImporter: '" + job.ModelPath.filename().string() + "': cannot decode embedded texture '" + sourceName + "'");
                    }
                } else {
                    fs::path relative = fs::path(key);
                    sourceName = relative.filename().string();
                    const fs::path modelDirectory = job.ModelPath.parent_path();
                    const fs::path candidates[] = { modelDirectory / relative, modelDirectory / relative.filename(),
                        modelDirectory / "textures" / relative.filename(), modelDirectory / "Textures" / relative.filename(),
                        relative.is_absolute() ? relative : fs::path() };
                    bool found = false;
                    for (const fs::path& candidate : candidates) {
                        std::error_code exists;
                        if (!candidate.empty() && fs::is_regular_file(candidate, exists) && TextureBaker::DecodeFile(candidate.string(), image)) { found = true; break; }
                    }
                    if (!found)
                        log(1, "ModelImporter: '" + job.ModelPath.filename().string() + "': texture '" + ref.Path + "' not found next to the model");
                }
                if (image.Width <= 0)
                    return {};
                const int sourceWidth = image.Width, sourceHeight = image.Height;
                TextureBaker::Image baked = TextureBaker::ResizeToPowerOfTwo(image, settings.MaxTextureSize);
                const unsigned index = static_cast<unsigned>(textureChildren.size());
                const std::string guid = chooseGuid("texture:" + key, "", 0);
                const std::string file = "texture_" + std::to_string(index) + ".png";
                const fs::path output = importDirectory / file;
                if (!ModelCooker::WriteFileAtomic(output.string(), TextureBaker::EncodePng(baked))) {
                    log(2, "ModelImporter: could not write '" + output.string() + "'");
                    return {};
                }
                WriteMeta(output, guid);
                TextureImportSettings textureSettings;
                textureSettings.FilterMode = TextureFilterMode::Bilinear;
                textureSettings.WrapMode = TextureWrapMode::Repeat; // model UVs commonly tile
                textureSettings.GenerateMipmaps = true;              // minified 3D surfaces shimmer without
                textureSettings.N3DSFormat = settings.TextureFormat;
                textureSettings.MaxSize = settings.MaxTextureSize;
                TextureImportSettings::Save(output.string(), textureSettings);
                producedFiles.insert(file);
                result.Registrations.push_back({ guid, output.string() });
                textureGuidByKey[key] = guid;
                ModelSubAssetInfo child{ "Texture", sourceName, std::to_string(sourceWidth) + "x" + std::to_string(sourceHeight) +
                    (baked.Width != sourceWidth || baked.Height != sourceHeight ? " -> " + std::to_string(baked.Width) + "x" + std::to_string(baked.Height) : std::string()) +
                    ", " + settings.TextureFormat, index, guid, output.string(), "texture:" + key };
                textureChildren.push_back(child);
                ModelBudgetTexture entry{ sourceName, sourceWidth, sourceHeight, baked.Width, baked.Height, settings.TextureFormat,
                    ModelCooker::N3DSTextureBytes(baked.Width, baked.Height, settings.TextureFormat, true) };
                budget.TextureBytes += entry.Bytes;
                if (static_cast<uint32_t>(std::max(baked.Width, baked.Height)) > limits.TextureSizeWarn)
                    budget.Warnings.push_back("Texture '" + sourceName + "' is " + std::to_string(baked.Width) + "x" + std::to_string(baked.Height) + " (> " + std::to_string(limits.TextureSizeWarn) + " px costs a lot of the 3DS's 6 MB VRAM)");
                budget.Textures.push_back(entry);
                return guid;
            };

            // ---- Materials -----------------------------------------------------------------------
            std::vector<std::string> materialGuids(model.Materials.size());
            std::vector<ModelSubAssetInfo> materialChildren;
            std::map<std::string, int> materialNameUses;
            std::set<uint32_t> usedMaterials;
            for (const ImportedMesh& mesh : model.Meshes) usedMaterials.insert(mesh.MaterialIndex);
            for (size_t i = 0; i < model.Materials.size(); ++i) {
                const ImportedMaterial& material = model.Materials[i];
                info.SourceMaterials.push_back(material.Name);
                if (!usedMaterials.count(static_cast<uint32_t>(i)) || settings.MaterialCreation == ModelMaterialCreation::None)
                    continue;
                const int use = materialNameUses[material.Name]++;
                const std::string key = "material:" + material.Name + (use ? "#" + std::to_string(use) : std::string());
                if (auto remap = settings.MaterialRemaps.find(material.Name); remap != settings.MaterialRemaps.end()) {
                    materialGuids[i] = remap->second;
                    materialChildren.push_back({ "Material", material.Name, "remapped", static_cast<unsigned>(i), remap->second, {}, key });
                    continue;
                }
                const std::string base = resolveTexture(material.BaseColorTexture);
                const std::string normal = resolveTexture(material.NormalTexture);
                const std::string guid = chooseGuid(key, "", 0);
                const std::string file = "material_" + std::to_string(i) + ".mat";
                const fs::path output = importDirectory / file;
                const Material mapped = ModelCooker::MapMaterial(material, base, normal);
                if (!ModelCooker::WriteTextAtomic(output.string(), MaterialLoader::ToJsonText(mapped))) {
                    log(2, "ModelImporter: could not write '" + output.string() + "'");
                    continue;
                }
                WriteMeta(output, guid);
                producedFiles.insert(file);
                result.Registrations.push_back({ guid, output.string() });
                materialGuids[i] = guid;
                materialChildren.push_back({ "Material", material.Name, std::string(GetShaderInfo(mapped.Shader).Name) + ", " + SurfaceTypeName(mapped.EffectiveSurface()),
                    static_cast<unsigned>(i), guid, output.string(), key });
            }
            progress(0.55f);

            // ---- Meshes --------------------------------------------------------------------------
            std::vector<std::string> meshKeys(model.Meshes.size());
            for (size_t node = 0; node < model.Nodes.size(); ++node)
                for (size_t slot = 0; slot < model.Nodes[node].Meshes.size(); ++slot) {
                    const uint32_t mesh = model.Nodes[node].Meshes[slot];
                    if (mesh < meshKeys.size() && meshKeys[mesh].empty())
                        meshKeys[mesh] = "mesh:" + NodePath(model, static_cast<int>(node)) + "/" + model.Meshes[mesh].Name + "#" + std::to_string(slot);
                }
            std::map<std::string, int> meshNameUses;
            for (const ImportedMesh& mesh : model.Meshes) meshNameUses[mesh.Name]++;
            std::vector<std::string> meshGuids(model.Meshes.size());
            std::vector<std::vector<std::string>> meshBoneNames(model.Meshes.size());
            std::vector<ModelSubAssetInfo> meshChildren;
            ModelCooker::MeshCookOptions meshOptions;
            meshOptions.Scale = scale;
            meshOptions.Skinned = rig;
            meshOptions.MaxBonesPerVertex = settings.MaxBonesPerVertex;
            meshOptions.ImportVertexColors = settings.ImportVertexColors;
            uint32_t skeletonBones = 0;
            for (size_t i = 0; i < model.Meshes.size(); ++i) {
                if (context.Stop && context.Stop->load()) { result.Error = "cancelled"; return; }
                const ImportedMesh& source = model.Meshes[i];
                if (meshKeys[i].empty()) meshKeys[i] = "mesh:#" + source.Name + "#" + std::to_string(i);
                const ModelCooker::CookedMesh cooked = ModelCooker::CookMesh(source, meshOptions);
                if (cooked.IndexCount() == 0) {
                    log(1, "ModelImporter: mesh '" + source.Name + "' has no triangles, skipped");
                    continue;
                }
                const std::string guid = chooseGuid(meshKeys[i], "Mesh", static_cast<unsigned>(i));
                const std::string file = "mesh_" + std::to_string(i) + ".dmesh";
                const fs::path output = importDirectory / file;
                ModelCooker::DMeshWriteOptions writeOptions;
                writeOptions.Quantize = settings.MeshCompression == ModelMeshCompression::On;
                writeOptions.ReadWrite = settings.ReadWrite;
                const std::vector<uint8_t> bytes = ModelCooker::EncodeDMesh(cooked, writeOptions);
                if (!ModelCooker::WriteFileAtomic(output.string(), bytes)) {
                    log(2, "ModelImporter: could not write '" + output.string() + "'");
                    continue;
                }
                WriteMeta(output, guid);
                producedFiles.insert(file);
                result.Registrations.push_back({ guid, output.string() });
                meshGuids[i] = guid;
                for (const MeshData::Bone& bone : cooked.Bones) meshBoneNames[i].push_back(bone.Name);
                skeletonBones = std::max(skeletonBones, static_cast<uint32_t>(cooked.Bones.size()));

                std::string display = source.Name.empty() ? "Mesh " + std::to_string(i) : source.Name;
                if (meshNameUses[source.Name] > 1 && source.MaterialIndex < model.Materials.size())
                    display += " (" + model.Materials[source.MaterialIndex].Name + ")";
                ModelBudgetMesh entry;
                entry.Name = display;
                entry.Vertices = cooked.UniqueVertexCount();
                entry.Triangles = cooked.Triangles;
                entry.RuntimeVertices = cooked.RuntimeVertexCount();
                entry.RuntimeBytes = static_cast<uint32_t>(cooked.RuntimeVertexCount() * sizeof(MeshVertex));
                entry.FileBytes = static_cast<uint32_t>(bytes.size());
                entry.SubMeshes = static_cast<uint32_t>(cooked.SubMeshes.size());
                entry.Bones = static_cast<uint32_t>(cooked.Bones.size());
                entry.MaxPaletteBones = cooked.MaxPaletteSize;
                entry.ClampedInfluences = cooked.ClampedInfluenceVertices;
                budget.Triangles += entry.Triangles;
                budget.RuntimeVertexBytes += entry.RuntimeBytes;
                budget.DrawCalls += entry.SubMeshes;
                budget.Meshes.push_back(entry);
                if (cooked.ClampedInfluenceVertices)
                    budget.Warnings.push_back("Mesh '" + display + "': " + std::to_string(cooked.ClampedInfluenceVertices) + " vertices had more than " +
                        std::to_string(settings.MaxBonesPerVertex) + " bone weights (strongest kept, renormalized)");
                if (cooked.UnweightedVertices)
                    budget.Warnings.push_back("Mesh '" + display + "': " + std::to_string(cooked.UnweightedVertices) + " skinned vertices had no weights (bound to a neighbouring bone)");
                meshChildren.push_back({ "Mesh", display, std::to_string(entry.Vertices) + " vertices, " + std::to_string(entry.Triangles) + " triangles, " +
                    std::to_string(entry.SubMeshes) + " draw" + (entry.SubMeshes == 1 ? "" : "s") +
                    (entry.Bones ? ", " + std::to_string(entry.Bones) + " bones (<= " + std::to_string(entry.MaxPaletteBones) + "/draw)" : std::string()),
                    static_cast<unsigned>(i), guid, output.string(), meshKeys[i] });
                progress(0.55f + 0.25f * static_cast<float>(i + 1) / static_cast<float>(model.Meshes.size()));
            }
            budget.Bones = skeletonBones;

            // ---- Animations ----------------------------------------------------------------------
            std::vector<ModelSubAssetInfo> clipChildren;
            std::string firstClipGuid;
            bool firstClipLoop = true;
            // Root motion node: the shallowest node with animated positions.
            std::unordered_map<std::string, int> nodeIndexByName;
            for (size_t n = 0; n < model.Nodes.size(); ++n) nodeIndexByName.emplace(model.Nodes[n].Name, static_cast<int>(n));
            auto depthOf = [&](int index) { int depth = 0; for (int c = index; c > 0; c = model.Nodes[static_cast<size_t>(c)].Parent) ++depth; return depth; };
            for (const ImportedAnimation& take : model.Animations) {
                info.SourceTakes.push_back({ take.Name, static_cast<float>(take.Duration) });
                for (const ImportedChannel& channel : take.Channels) {
                    if (channel.Positions.size() < 2) continue;
                    auto found = nodeIndexByName.find(channel.Node);
                    if (found == nodeIndexByName.end()) continue;
                    auto current = nodeIndexByName.find(info.RootMotionNode);
                    if (info.RootMotionNode.empty() || current == nodeIndexByName.end() || depthOf(found->second) < depthOf(current->second))
                        info.RootMotionNode = channel.Node;
                }
            }
            if (settings.ImportAnimation && !model.Animations.empty()) {
                std::vector<ModelClipSettings> clips = settings.Clips;
                if (clips.empty())
                    for (const ImportedAnimation& take : model.Animations)
                        clips.push_back({ take.Name, take.Name, 0.0f, -1.0f, true });
                ModelCooker::ClipBakeOptions bake;
                bake.SampleRate = settings.SampleRate;
                bake.Scale = scale;
                bake.KeyframeReduction = settings.KeyframeReduction;
                bake.PositionErrorPercent = settings.PositionError;
                bake.RotationErrorDegrees = settings.RotationError;
                bake.ScaleErrorPercent = settings.ScaleError;
                bake.RemoveRootMotion = settings.RemoveRootMotion;
                bake.RootMotionNode = info.RootMotionNode;
                std::map<std::string, int> clipNameUses;
                for (size_t c = 0; c < clips.size(); ++c) {
                    const ModelClipSettings& clip = clips[c];
                    const ImportedAnimation* take = &model.Animations.front();
                    for (const ImportedAnimation& candidate : model.Animations)
                        if (candidate.Name == clip.Take) { take = &candidate; break; }
                    const std::string clipName = clip.Name.empty() ? take->Name : clip.Name;
                    const int use = clipNameUses[clipName]++;
                    const std::string key = "clip:" + clipName + (use ? "#" + std::to_string(use) : std::string());
                    AnimationClipData baked = ModelCooker::BakeClip(*take, clip, bake);
                    // The prefab root is named after the file (Unity): retarget root-node channels.
                    for (AnimationChannel& channel : baked.Channels)
                        if (!model.Nodes.empty() && channel.NodeName == model.Nodes.front().Name)
                            channel.NodeName = stem;
                    uint32_t keys = 0;
                    for (const AnimationChannel& channel : baked.Channels)
                        keys += static_cast<uint32_t>(channel.PositionKeys.size() + channel.RotationKeys.size() + channel.ScaleKeys.size());
                    const std::string guid = chooseGuid(key, "Animation", static_cast<unsigned>(c));
                    const std::string file = "animation_" + std::to_string(c) + ".anim";
                    const fs::path output = importDirectory / file;
                    json document = json::parse(AnimationClipLoader::ToJson(baked));
                    document["Version"] = 2;
                    document["Name"] = clipName;
                    if (!ModelCooker::WriteTextAtomic(output.string(), document.dump(1))) {
                        log(2, "ModelImporter: could not write '" + output.string() + "'");
                        continue;
                    }
                    WriteMeta(output, guid);
                    producedFiles.insert(file);
                    result.Registrations.push_back({ guid, output.string() });
                    if (firstClipGuid.empty()) { firstClipGuid = guid; firstClipLoop = clip.Loop; }
                    budget.Clips++;
                    budget.ClipKeys += keys;
                    char seconds[32];
                    std::snprintf(seconds, sizeof(seconds), "%.2f s", baked.Duration);
                    clipChildren.push_back({ "Animation", clipName, std::string(seconds) + ", " + std::to_string(baked.Channels.size()) + " channels, " +
                        std::to_string(keys) + " keys" + (clip.Loop ? ", loop" : ""), static_cast<unsigned>(c), guid, output.string(), key });
                }
            }
            progress(0.9f);

            // ---- Inventory ----------------------------------------------------------------------
            info.Children = meshChildren;
            info.Children.insert(info.Children.end(), materialChildren.begin(), materialChildren.end());
            info.Children.insert(info.Children.end(), textureChildren.begin(), textureChildren.end());
            info.Children.insert(info.Children.end(), clipChildren.begin(), clipChildren.end());
            if (rig && skeletonBones > 0)
                info.Children.push_back({ "Avatar", stem + "Avatar", std::to_string(skeletonBones) + " bones, Generic", 0, {}, {}, "avatar" });

            // ---- Budget warnings ------------------------------------------------------------------
            if (budget.Triangles > limits.TrianglesWarn)
                budget.Warnings.push_back(std::to_string(budget.Triangles) + " triangles: heavy for the 3DS (aim for < " + std::to_string(limits.TrianglesWarn) + " per model on screen)");
            if (budget.RuntimeVertexBytes > limits.RuntimeVertexBytesWarn)
                budget.Warnings.push_back("Vertex buffers need " + std::to_string(budget.RuntimeVertexBytes / 1024) + " KB of 3DS linear memory");
            if (budget.TextureBytes > limits.TextureBytesWarn)
                budget.Warnings.push_back("Textures need " + std::to_string(budget.TextureBytes / 1024) + " KB of VRAM (the 3DS has 6 MB in total)");
            if (budget.DrawCalls > limits.DrawCallsWarn)
                budget.Warnings.push_back(std::to_string(budget.DrawCalls) + " draw calls for one model");

            // ---- Prefab plan --------------------------------------------------------------------
            ModelPrefabPlan& plan = result.Prefab;
            for (size_t n = 0; n < model.Nodes.size(); ++n) {
                const ImportedNode& node = model.Nodes[n];
                ModelPrefabNode out;
                out.Name = n == 0 ? stem : node.Name;
                out.Parent = node.Parent;
                out.Translation = node.Translation * scale;
                out.Rotation = node.Rotation;
                out.Scale = node.Scale;
                for (uint32_t meshIndex : node.Meshes) {
                    if (meshIndex >= meshGuids.size() || meshGuids[meshIndex].empty()) continue;
                    const ImportedMesh& source = model.Meshes[meshIndex];
                    ModelPrefabMesh mesh;
                    mesh.Name = source.Name.empty() ? node.Name : source.Name;
                    mesh.MeshGuid = meshGuids[meshIndex];
                    if (source.MaterialIndex < materialGuids.size() && !materialGuids[source.MaterialIndex].empty())
                        mesh.MaterialGuids.push_back(materialGuids[source.MaterialIndex]);
                    mesh.Skinned = rig && !meshBoneNames[meshIndex].empty();
                    for (const std::string& bone : meshBoneNames[meshIndex]) {
                        auto found = nodeIndexByName.find(bone);
                        mesh.BoneNodes.push_back(found == nodeIndexByName.end() ? -1 : found->second);
                    }
                    out.Meshes.push_back(std::move(mesh));
                }
                plan.Nodes.push_back(std::move(out));
            }
            plan.ClipGuid = firstClipGuid;
            plan.ClipLoop = firstClipLoop;
            result.PrefabGuid = !previous.PrefabGuid.empty() ? previous.PrefabGuid : ModelCooker::StableGuid(job.ModelGuid, "prefab");
            producedFiles.insert("model.prefab");
            info.PrefabPath = (importDirectory / "model.prefab").string();

            // ---- Remove outputs of the previous import that this one no longer produces --------------
            for (const auto& entry : fs::directory_iterator(importDirectory, error)) {
                if (!entry.is_regular_file()) continue;
                std::string name = entry.path().filename().string();
                std::string owner = name;
                if (LowerExtension(name) == ".meta") owner = name.substr(0, name.size() - 5);
                if (LowerExtension(name) == ".tmp" || !producedFiles.count(owner)) {
                    std::error_code removeError;
                    fs::remove(entry.path(), removeError);
                }
            }
            result.Success = true;
            progress(0.95f);
        }

        void RunScan(const Job& job, JobResult& result) {
            result.Type = Job::Kind::Scan;
            std::error_code error;
            for (auto it = fs::recursive_directory_iterator(job.ScanRoot, fs::directory_options::skip_permission_denied, error);
                 !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
                if (it->is_directory() && it->path().filename() == ".duality-import") {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file() || !ModelImporter::IsSupported(it->path()))
                    continue;
                ScannedModel model;
                model.Path = it->path();
                model.Key = KeyOf(it->path());
                model.Modified = ModifiedTicks(it->path(), &model.Size);
                result.Scanned.push_back(std::move(model));
            }
        }

        // ---- Shared state ----------------------------------------------------------------------
        struct State {
            // Main thread only.
            std::unordered_map<std::string, std::unique_ptr<Entry>> Entries;
            std::function<void(const std::string&)> Listener;
            std::chrono::steady_clock::time_point LastScan{};
            bool ScanPending = false;
            std::thread::id MainThread{};

            // Guarded by Mutex.
            std::mutex Mutex;
            std::condition_variable WorkAvailable;
            std::condition_variable Finished;
            std::deque<Job> Jobs;
            std::vector<std::unique_ptr<JobResult>> Results;
            std::vector<ExternalRequest> External;
            std::set<std::string> DoneKeys; // EnsureImported waiters
            std::string ActiveLabel;
            int ActiveJobs = 0;
            std::atomic<float> Progress{ 0.0f };
            std::atomic<bool> Stop{ false };
            std::thread Worker;

            ~State() {
                {
                    std::lock_guard<std::mutex> lock(Mutex);
                    Stop = true;
                }
                WorkAvailable.notify_all();
                if (Worker.joinable())
                    Worker.join();
            }

            void EnsureWorker() {
                if (Worker.joinable())
                    return;
                Worker = std::thread([this]() {
                    for (;;) {
                        Job job;
                        {
                            std::unique_lock<std::mutex> lock(Mutex);
                            WorkAvailable.wait(lock, [this]() { return Stop.load() || !Jobs.empty(); });
                            if (Stop.load())
                                return;
                            job = std::move(Jobs.front());
                            Jobs.pop_front();
                            ++ActiveJobs;
                            ActiveLabel = job.Type == Job::Kind::Scan ? std::string() : job.ModelPath.filename().string();
                            Progress = 0.0f;
                        }
                        auto result = std::make_unique<JobResult>();
                        WorkerContext context{ &Progress, &Stop };
                        try {
                            if (job.Type == Job::Kind::Scan) RunScan(job, *result);
                            else RunImport(job, *result, context);
                        } catch (const std::exception& exception) {
                            result->Success = false;
                            result->Error = std::string("importer exception: ") + exception.what();
                        }
                        result->Type = job.Type;
                        result->Key = job.Key;
                        result->ModelPath = job.ModelPath;
                        result->ModelGuid = job.ModelGuid;
                        {
                            std::lock_guard<std::mutex> lock(Mutex);
                            Results.push_back(std::move(result));
                            --ActiveJobs;
                            ActiveLabel.clear();
                        }
                    }
                });
            }
        };

        State& Shared() {
            static State state;
            return state;
        }

        ModelImportInfo InfoFromInventory(const json& block, const fs::path& importDirectory) {
            ModelImportInfo info;
            if (!block.is_object())
                return info;
            info.Valid = block.value("Error", std::string()).empty();
            info.Error = block.value("Error", std::string());
            for (const json& child : block.value("Children", json::array())) {
                if (!child.is_object()) continue;
                ModelSubAssetInfo entry;
                entry.Type = child.value("Type", std::string());
                entry.Name = child.value("Name", std::string());
                entry.Detail = child.value("Detail", std::string());
                entry.Index = child.value("Index", 0u);
                entry.AssetGuid = child.value("AssetGuid", std::string());
                entry.Key = child.value("Key", std::string());
                const std::string file = child.value("File", std::string());
                entry.AssetPath = !file.empty() && child.value("Generated", false) ? (importDirectory / file).string() : child.value("AssetPath", std::string());
                info.Children.push_back(std::move(entry));
            }
            if (block.contains("Prefab") && block["Prefab"].is_object())
                info.PrefabPath = (importDirectory / block["Prefab"].value("File", std::string("model.prefab"))).string();
            info.Budget = BudgetFromJson(block.value("Budget", json::object()));
            for (const json& name : block.value("SourceMaterials", json::array()))
                if (name.is_string()) info.SourceMaterials.push_back(name.get<std::string>());
            for (const json& take : block.value("SourceTakes", json::array()))
                info.SourceTakes.push_back({ take.value("Name", std::string()), take.value("Duration", 0.0f) });
            info.RootMotionNode = block.value("RootMotionNode", std::string());
            info.FileUnitScale = block.value("FileUnitScale", 1.0f);
            for (const json& warning : block.value("Warnings", json::array()))
                if (warning.is_string()) info.Warnings.push_back(warning.get<std::string>());
            return info;
        }

        json InventoryFromResult(const JobResult& result, const fs::path& modelPath, const fs::path& importDirectory) {
            json children = json::array();
            for (const ModelSubAssetInfo& child : result.Info.Children) {
                const bool generated = !child.AssetPath.empty() && fs::path(child.AssetPath).parent_path().lexically_normal() == importDirectory.lexically_normal();
                children.push_back({ { "Type", child.Type }, { "Name", child.Name }, { "Detail", child.Detail }, { "Index", child.Index },
                    { "Key", child.Key }, { "AssetGuid", child.AssetGuid }, { "Generated", generated },
                    { "File", generated ? fs::path(child.AssetPath).filename().string() : std::string() },
                    { "AssetPath", child.AssetPath } });
            }
            json takes = json::array();
            for (const auto& take : result.Info.SourceTakes) takes.push_back({ { "Name", take.Name }, { "Duration", take.Duration } });
            json block = {
                { "Version", InventoryVersion }, { "ImporterVersion", ModelImporter::ImporterVersion },
                { "SettingsHash", result.SettingsHash },
                { "Source", { { "Size", result.SourceSize }, { "Modified", std::to_string(result.SourceModified) }, { "Hash", result.SourceHash } } },
                { "ImportDirectory", fs::path(importDirectory).lexically_relative(modelPath.parent_path()).generic_string() },
                { "Prefab", { { "Guid", result.PrefabGuid }, { "File", "model.prefab" } } },
                { "Children", children }, { "Budget", BudgetToJson(result.Info.Budget) },
                { "SourceMaterials", result.Info.SourceMaterials }, { "SourceTakes", takes },
                { "RootMotionNode", result.Info.RootMotionNode }, { "FileUnitScale", result.Info.FileUnitScale },
                { "Warnings", result.Info.Warnings }, { "Error", std::string() }
            };
            return block;
        }

        Entry& GetEntry(const fs::path& path) {
            State& state = Shared();
            const std::string key = KeyOf(path);
            auto found = state.Entries.find(key);
            if (found == state.Entries.end()) {
                found = state.Entries.emplace(key, std::make_unique<Entry>()).first;
                found->second->Path = path;
            }
            return *found->second;
        }

        void QueueImport(Entry& entry, bool force, bool front = false) {
            State& state = Shared();
            const std::string guid = AssetMeta::EnsureMetaFile(entry.Path);
            if (guid.empty())
                return;
            AssetDatabase::Register(guid, entry.Path.string());
            const json meta = ReadJsonFile(entry.Path.string() + ".meta");
            Job job;
            job.Type = Job::Kind::Import;
            job.Key = KeyOf(entry.Path);
            job.ModelPath = entry.Path;
            job.ModelGuid = guid;
            job.Settings = ModelImportSettings::FromJson(meta.value("ModelImporterSettings", json::object()));
            job.Previous = meta.value("ModelImporter", json::object());
            job.Force = force;
            {
                std::lock_guard<std::mutex> lock(state.Mutex);
                // Coalesce: a newer request for the same model replaces a queued one.
                for (auto it = state.Jobs.begin(); it != state.Jobs.end();)
                    if (it->Type == Job::Kind::Import && it->Key == job.Key) { job.Force = job.Force || it->Force; it = state.Jobs.erase(it); }
                    else ++it;
                if (front) state.Jobs.push_front(std::move(job));
                else state.Jobs.push_back(std::move(job));
            }
            entry.Queued = true;
            if (entry.State != ModelImportState::Importing)
                entry.State = ModelImportState::Queued;
            state.EnsureWorker();
            state.WorkAvailable.notify_one();
        }

        // Reads <model>.meta once: up-to-date (fast path: same size + mtime + settings + version
        // and every output present) or queue a background check/import.
        void Evaluate(Entry& entry) {
            entry.Evaluated = true;
            entry.ObservedModified = ModifiedTicks(entry.Path, &entry.ObservedSize);
            const std::string guid = AssetMeta::EnsureMetaFile(entry.Path);
            const json meta = ReadJsonFile(entry.Path.string() + ".meta");
            const json block = meta.value("ModelImporter", json::object());
            const fs::path importDirectory = ModelAsset::ImportDirectory(entry.Path.string(), guid);
            const ModelImportSettings settings = ModelImportSettings::FromJson(meta.value("ModelImporterSettings", json::object()));
            if (block.value("Version", 1) == InventoryVersion) {
                entry.Info = InfoFromInventory(block, importDirectory);
                for (const ModelSubAssetInfo& child : entry.Info.Children)
                    if (!child.AssetGuid.empty() && !child.AssetPath.empty()) AssetDatabase::Register(child.AssetGuid, child.AssetPath);
            } else if (block.is_object() && block.contains("Children")) {
                // First-importer inventory: show it until the upgrade import replaces it.
                for (const json& child : block["Children"]) {
                    ModelSubAssetInfo legacy{ child.value("Type", std::string()), child.value("Name", std::string()), child.value("Detail", std::string()),
                        child.value("Index", 0u), child.value("AssetGuid", std::string()), child.value("AssetPath", std::string()), {} };
                    entry.Info.Children.push_back(legacy);
                }
                entry.Info.Valid = true;
            }
            const bool upToDate = block.value("Version", 1) == InventoryVersion &&
                block.value("ImporterVersion", 0) == ModelImporter::ImporterVersion &&
                block.value("SettingsHash", std::string()) == settings.Hash() &&
                block.contains("Source") && block["Source"].value("Size", static_cast<uintmax_t>(0)) == entry.ObservedSize &&
                block["Source"].value("Modified", std::string()) == std::to_string(entry.ObservedModified) &&
                block.value("Error", std::string()).empty() && OutputsExist(block, importDirectory);
            if (upToDate) {
                entry.State = ModelImportState::Imported;
                return;
            }
            QueueImport(entry, /*force=*/false);
        }

        void NotifyDone(const std::string& key) {
            State& state = Shared();
            {
                std::lock_guard<std::mutex> lock(state.Mutex);
                state.DoneKeys.insert(key);
            }
            state.Finished.notify_all();
        }

        void Finalize(JobResult& result) {
            State& state = Shared();
            for (const auto& [level, message] : result.Logs) {
                if (level == 0) Log::Info(message);
                else if (level == 1) Log::Warn(message);
                else Log::Error(message);
            }
            Entry& entry = GetEntry(result.ModelPath);
            entry.Queued = false;
            const fs::path metaPath = result.ModelPath.string() + ".meta";
            if (!result.Success) {
                // Keep the previous outputs (and their inventory): scenes using them keep working.
                entry.State = ModelImportState::Failed;
                entry.Info.Error = result.Error;
                Log::Error("ModelImporter: could not import '" + result.ModelPath.string() + "': " + result.Error);
                NotifyDone(result.Key);
                return;
            }
            json meta = ReadJsonFile(metaPath);
            const fs::path importDirectory = ModelAsset::ImportDirectory(result.ModelPath.string(), result.ModelGuid);
            if (result.UpToDate) {
                // Same content (e.g. a fresh checkout touched the mtime): refresh the fast-path key.
                if (meta.contains("ModelImporter") && meta["ModelImporter"].is_object()) {
                    meta["ModelImporter"]["Source"]["Modified"] = std::to_string(result.SourceModified);
                    meta["ModelImporter"]["Source"]["Size"] = result.SourceSize;
                    ModelCooker::WriteTextAtomic(metaPath.string(), meta.dump(2));
                    entry.Info = InfoFromInventory(meta["ModelImporter"], importDirectory);
                    for (const ModelSubAssetInfo& child : entry.Info.Children)
                        if (!child.AssetGuid.empty() && !child.AssetPath.empty()) AssetDatabase::Register(child.AssetGuid, child.AssetPath);
                }
                entry.State = ModelImportState::Imported;
                NotifyDone(result.Key);
                return;
            }

            // A model moved to another folder leaves its old generated folder behind.
            if (meta.contains("ModelImporter") && meta["ModelImporter"].is_object()) {
                const std::string previousDirectory = meta["ModelImporter"].value("ImportDirectory", std::string());
                if (!previousDirectory.empty()) {
                    const fs::path old = (result.ModelPath.parent_path() / previousDirectory).lexically_normal();
                    std::error_code error;
                    if (old.filename() == result.ModelGuid && old != importDirectory.lexically_normal() && fs::is_directory(old, error))
                        fs::remove_all(old, error);
                }
            }

            for (const auto& [guid, path] : result.Registrations) {
                AssetDatabase::Register(guid, path);
                const std::string extension = LowerExtension(path);
                if (extension == ".dmesh") MeshLoader::Invalidate(path);
                else if (extension == ".anim") AnimationClipLoader::Invalidate(path);
            }
            MaterialLoader::ClearCache();

            const std::string prefabPath = (importDirectory / "model.prefab").string();
            if (ModelPrefabBuilder::Build(result.Prefab, prefabPath)) {
                WriteMeta(prefabPath, result.PrefabGuid);
                AssetDatabase::Register(result.PrefabGuid, prefabPath);
            } else {
                result.Info.Warnings.push_back("could not write the model prefab");
            }
            result.Info.Warnings.insert(result.Info.Warnings.end(), result.Info.Budget.Warnings.begin(), result.Info.Budget.Warnings.end());
            meta["guid"] = result.ModelGuid;
            meta["ModelImporter"] = InventoryFromResult(result, result.ModelPath, importDirectory);
            ModelCooker::WriteTextAtomic(metaPath.string(), meta.dump(2));

            entry.Info = std::move(result.Info);
            entry.Info.PrefabPath = prefabPath;
            entry.State = ModelImportState::Imported;
            entry.ObservedModified = result.SourceModified;
            entry.ObservedSize = result.SourceSize;
            Log::Info("ModelImporter: imported '" + result.ModelPath.filename().string() + "' (" +
                std::to_string(entry.Info.Budget.Triangles) + " triangles, " + std::to_string(entry.Info.Children.size()) + " sub-assets)");
            if (state.Listener)
                state.Listener(result.ModelPath.string());
            NotifyDone(result.Key);
        }

        void ProcessResults() {
            State& state = Shared();
            std::vector<std::unique_ptr<JobResult>> results;
            {
                std::lock_guard<std::mutex> lock(state.Mutex);
                results.swap(state.Results);
            }
            for (auto& result : results) {
                if (result->Type == Job::Kind::Scan) {
                    state.ScanPending = false;
                    std::unordered_set<std::string> seen;
                    for (const ScannedModel& scanned : result->Scanned) {
                        seen.insert(scanned.Key);
                        Entry& entry = GetEntry(scanned.Path);
                        if (!entry.Evaluated) {
                            Evaluate(entry);
                        } else if (!entry.Queued && (scanned.Size != entry.ObservedSize || scanned.Modified != entry.ObservedModified)) {
                            entry.ObservedSize = scanned.Size;
                            entry.ObservedModified = scanned.Modified;
                            QueueImport(entry, /*force=*/false);
                        }
                    }
                    // Deleted models: drop the entry and the generated folder once the .meta is gone too.
                    for (auto it = state.Entries.begin(); it != state.Entries.end();) {
                        std::error_code error;
                        if (!seen.count(it->first) && !fs::exists(it->second->Path, error) && !it->second->Queued) {
                            fs::path meta = it->second->Path;
                            meta += ".meta";
                            if (!fs::exists(meta, error)) {
                                for (const ModelSubAssetInfo& child : it->second->Info.Children) {
                                    if (child.AssetPath.empty()) continue;
                                    const fs::path directory = fs::path(child.AssetPath).parent_path();
                                    if (directory.parent_path().filename() == ".duality-import") { fs::remove_all(directory, error); break; }
                                }
                            }
                            it = state.Entries.erase(it);
                        } else {
                            ++it;
                        }
                    }
                } else {
                    Finalize(*result);
                }
            }
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
        Entry& entry = GetEntry(path);
        if (!entry.Evaluated) {
            Evaluate(entry);
        } else if (!entry.Queued && entry.State != ModelImportState::Importing) {
            uintmax_t size = 0;
            const int64_t modified = ModifiedTicks(path, &size);
            if (modified != 0 && (modified != entry.ObservedModified || size != entry.ObservedSize)) {
                entry.ObservedModified = modified;
                entry.ObservedSize = size;
                QueueImport(entry, /*force=*/false);
            }
        }
        return entry.Info;
    }

    ModelImportState ModelImporter::GetState(const std::filesystem::path& path, float* progress) {
        State& state = Shared();
        auto found = state.Entries.find(KeyOf(path));
        if (found == state.Entries.end())
            return ModelImportState::Unknown;
        Entry& entry = *found->second;
        ModelImportState result = entry.State;
        if (entry.Queued) {
            std::lock_guard<std::mutex> lock(state.Mutex);
            const bool active = !state.ActiveLabel.empty() && state.ActiveLabel == entry.Path.filename().string();
            result = active ? ModelImportState::Importing : ModelImportState::Queued;
            if (progress) *progress = active ? state.Progress.load() : 0.0f;
        } else if (progress) {
            *progress = result == ModelImportState::Imported ? 1.0f : 0.0f;
        }
        return result;
    }

    void ModelImporter::RequestImport(const std::filesystem::path& path, bool force) {
        if (!IsSupported(path))
            return;
        Entry& entry = GetEntry(path);
        entry.Evaluated = true;
        entry.ObservedModified = ModifiedTicks(path, &entry.ObservedSize);
        QueueImport(entry, force, /*front=*/true);
    }

    void ModelImporter::Invalidate(const std::filesystem::path& path) {
        State& state = Shared();
        auto found = state.Entries.find(KeyOf(path));
        if (found != state.Entries.end() && !found->second->Queued)
            found->second->Evaluated = false;
    }

    bool ModelImporter::ImportNow(const std::filesystem::path& path, bool force) {
        if (!IsSupported(path))
            return false;
        State& state = Shared();
        Entry& entry = GetEntry(path);
        const std::string guid = AssetMeta::EnsureMetaFile(path);
        if (guid.empty())
            return false;
        AssetDatabase::Register(guid, path.string());
        const json meta = ReadJsonFile(path.string() + ".meta");
        Job job;
        job.Key = KeyOf(path);
        job.ModelPath = path;
        job.ModelGuid = guid;
        job.Settings = ModelImportSettings::FromJson(meta.value("ModelImporterSettings", json::object()));
        job.Previous = meta.value("ModelImporter", json::object());
        job.Force = force;
        JobResult result;
        std::atomic<float> progress{ 0.0f };
        std::atomic<bool> stop{ false };
        WorkerContext context{ &progress, &stop };
        RunImport(job, result, context);
        result.Key = job.Key;
        result.ModelPath = path;
        result.ModelGuid = guid;
        entry.Evaluated = true;
        Finalize(result);
        (void)state;
        return entry.State == ModelImportState::Imported;
    }

    void ModelImporter::EnsureImported(const std::filesystem::path& path) {
        if (!IsSupported(path))
            return;
        State& state = Shared();
        if (state.MainThread == std::thread::id{} || state.MainThread == std::this_thread::get_id()) {
            Inspect(path);
            Entry& entry = GetEntry(path);
            // A background job for it may be running: let it finish rather than import twice.
            for (int i = 0; i < 30000 && entry.Queued; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                ProcessResults();
            }
            if (entry.State != ModelImportState::Imported)
                ImportNow(path);
            return;
        }
        const std::string key = KeyOf(path);
        std::unique_lock<std::mutex> lock(state.Mutex);
        state.DoneKeys.erase(key);
        state.External.push_back({ key, path });
        // The editor's main thread picks the request up on its next Tick (normally < 20 ms).
        state.Finished.wait_for(lock, std::chrono::minutes(10), [&]() { return state.DoneKeys.count(key) != 0 || state.Stop.load(); });
    }

    void ModelImporter::Tick(const std::filesystem::path& assetsRoot) {
        State& state = Shared();
        state.MainThread = std::this_thread::get_id();
        ProcessResults();

        std::vector<ExternalRequest> external;
        {
            std::lock_guard<std::mutex> lock(state.Mutex);
            external.swap(state.External);
        }
        for (const ExternalRequest& request : external) {
            Entry& entry = GetEntry(request.Path);
            if (!entry.Evaluated) Evaluate(entry);
            if (entry.State == ModelImportState::Imported || entry.State == ModelImportState::Failed) {
                if (!entry.Queued) NotifyDone(request.Key);
            } else if (!entry.Queued) {
                QueueImport(entry, false, true);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        std::error_code error;
        if (!assetsRoot.empty() && !state.ScanPending && now - state.LastScan > std::chrono::seconds(2) && fs::is_directory(assetsRoot, error)) {
            state.LastScan = now;
            state.ScanPending = true;
            Job job;
            job.Type = Job::Kind::Scan;
            job.ScanRoot = assetsRoot;
            {
                std::lock_guard<std::mutex> lock(state.Mutex);
                state.Jobs.push_back(std::move(job));
            }
            state.EnsureWorker();
            state.WorkAvailable.notify_one();
        }
    }

    void ModelImporter::SetReimportListener(std::function<void(const std::string&)> listener) {
        Shared().Listener = std::move(listener);
    }

    int ModelImporter::PendingJobCount() {
        State& state = Shared();
        std::lock_guard<std::mutex> lock(state.Mutex);
        int count = state.ActiveJobs;
        for (const Job& job : state.Jobs) count += job.Type == Job::Kind::Import ? 1 : 0;
        return count;
    }

    std::string ModelImporter::ActiveJobLabel(float* progress) {
        State& state = Shared();
        std::lock_guard<std::mutex> lock(state.Mutex);
        if (progress) *progress = state.Progress.load();
        return state.ActiveLabel;
    }

    bool ModelImporter::IsIdle() {
        State& state = Shared();
        bool queued = false;
        for (const auto& [key, entry] : state.Entries) queued |= entry->Queued;
        std::lock_guard<std::mutex> lock(state.Mutex);
        return !queued && state.ActiveJobs == 0 && state.Results.empty() &&
            std::none_of(state.Jobs.begin(), state.Jobs.end(), [](const Job& job) { return job.Type == Job::Kind::Import; });
    }

    namespace {
        int ExtractChildren(const fs::path& modelPath, const fs::path& targetFolder, const char* type, const char* extension) {
            const ModelImportInfo info = ModelImporter::Inspect(modelPath);
            ModelImportSettings settings = ModelImportSettings::Load(modelPath.string());
            std::error_code error;
            fs::create_directories(targetFolder, error);
            int extracted = 0;
            for (const ModelSubAssetInfo& child : info.Children) {
                if (child.Type != type || child.AssetGuid.empty() || child.AssetPath.empty() ||
                    fs::path(child.AssetPath).parent_path().parent_path().filename() != ".duality-import")
                    continue;
                std::string base = SanitizeFileName(child.Name);
                fs::path destination = targetFolder / (base + extension);
                for (int suffix = 1; fs::exists(destination, error); ++suffix)
                    destination = targetFolder / (base + " (" + std::to_string(suffix) + ")" + extension);
                fs::copy_file(child.AssetPath, destination, error);
                if (error) {
                    Log::Error("ModelImporter: could not extract '" + child.AssetPath + "': " + error.message());
                    continue;
                }
                // Same GUID: every material / scene object already pointing at the generated asset
                // now points at the editable copy.
                fs::copy_file(child.AssetPath + ".meta", destination.string() + ".meta", fs::copy_options::overwrite_existing, error);
                if (error) WriteMeta(destination, child.AssetGuid);
                fs::remove(child.AssetPath, error);
                fs::remove(child.AssetPath + ".meta", error);
                AssetDatabase::Register(child.AssetGuid, destination.string());
                const std::string prefix = std::string(type) == "Material" ? "material:" : "texture:";
                std::string source = child.Key.rfind(prefix, 0) == 0 ? child.Key.substr(prefix.size()) : child.Name;
                if (std::string(type) == "Material") {
                    const size_t hash = source.rfind('#');
                    if (hash != std::string::npos) source.resize(hash);
                    settings.MaterialRemaps[source] = child.AssetGuid;
                } else {
                    settings.TextureRemaps[source] = child.AssetGuid;
                }
                ++extracted;
                Log::Info("ModelImporter: extracted " + std::string(type) + " '" + child.Name + "' to '" + destination.string() + "'");
            }
            if (extracted > 0) {
                ModelImportSettings::Save(modelPath.string(), settings);
                ModelImporter::RequestImport(modelPath, true);
            }
            return extracted;
        }
    }

    int ModelImporter::ExtractMaterials(const std::filesystem::path& modelPath, const std::filesystem::path& targetFolder) {
        return ExtractChildren(modelPath, targetFolder, "Material", ".mat");
    }

    int ModelImporter::ExtractTextures(const std::filesystem::path& modelPath, const std::filesystem::path& targetFolder) {
        return ExtractChildren(modelPath, targetFolder, "Texture", ".png");
    }
}
