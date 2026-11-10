#include "TestFramework.h"

#include <cctype>
#include <cmath>
#include <map>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include <nlohmann/json.hpp>

#include "DualityEditor/Import/ModelCooker.h"
#include "DualityEditor/Import/ModelImportSettings.h"
#include "DualityEditor/Import/TextureBaker.h"
#include "DualityEditor/ModelImporter.h"
#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/ModelAsset.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

using namespace Duality;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
    bool IsHexGuid(const std::string& value) {
        if (value.size() != 32) return false;
        for (char c : value) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        return true;
    }

    // A strip of quads along +X, `bones` bones, each column of vertices weighted to up to 6
    // neighbouring bones (more than the 4 the 3DS shader reads).
    ImportedMesh MakeSkinnedStrip(int columns, int bones) {
        ImportedMesh mesh;
        mesh.Name = "Strip";
        for (int x = 0; x <= columns; ++x)
            for (int y = 0; y < 2; ++y) {
                mesh.Positions.push_back({ static_cast<float>(x), static_cast<float>(y), 0.0f });
                mesh.Normals.push_back({ 0.0f, 0.0f, 1.0f });
                mesh.UVs.push_back({ x / static_cast<float>(columns), static_cast<float>(y) });
            }
        for (int x = 0; x < columns; ++x) {
            const uint32_t a = x * 2, b = a + 1, c = a + 2, d = a + 3;
            mesh.Indices.insert(mesh.Indices.end(), { a, c, b, b, c, d });
        }
        mesh.Bones.resize(static_cast<size_t>(bones));
        for (int bone = 0; bone < bones; ++bone) {
            mesh.Bones[static_cast<size_t>(bone)].Name = "Bone" + std::to_string(bone);
            mesh.Bones[static_cast<size_t>(bone)].Offset = glm::mat4(1.0f);
        }
        const uint32_t vertexCount = static_cast<uint32_t>(mesh.Positions.size());
        for (uint32_t v = 0; v < vertexCount; ++v) {
            const int column = static_cast<int>(v / 2);
            const int center = column * bones / (columns + 1);
            for (int k = 0; k < 6; ++k) {
                const int bone = std::min(bones - 1, center + k);
                mesh.Bones[static_cast<size_t>(bone)].Weights.push_back({ v, 1.0f / (k + 1.0f) });
            }
        }
        return mesh;
    }

    std::string Base64(const std::vector<uint8_t>& data) {
        static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < data.size(); i += 3) {
            const uint32_t n = (data[i] << 16) | ((i + 1 < data.size() ? data[i + 1] : 0) << 8) | (i + 2 < data.size() ? data[i + 2] : 0);
            out += table[(n >> 18) & 63];
            out += table[(n >> 12) & 63];
            out += i + 1 < data.size() ? table[(n >> 6) & 63] : '=';
            out += i + 2 < data.size() ? table[n & 63] : '=';
        }
        return out;
    }

    template<typename T>
    size_t Push(std::vector<uint8_t>& buffer, const std::vector<T>& values) {
        while (buffer.size() % 4) buffer.push_back(0);
        const size_t offset = buffer.size();
        const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
        buffer.insert(buffer.end(), bytes, bytes + values.size() * sizeof(T));
        return offset;
    }

    // Writes a small skinned + animated + textured glTF 2.0 model (everything embedded as data
    // URIs) -- a real file for Assimp, small enough to keep inside the test.
    void WriteSkinnedGltf(const fs::path& path) {
        std::vector<uint8_t> buffer;
        json views = json::array(), accessors = json::array();
        auto add = [&](size_t offset, size_t bytes, int componentType, int count, const char* type, json extra = json::object()) {
            views.push_back({ { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", bytes } });
            json accessor = { { "bufferView", views.size() - 1 }, { "componentType", componentType }, { "count", count }, { "type", type } };
            for (auto& [key, value] : extra.items()) accessor[key] = value;
            accessors.push_back(accessor);
            return static_cast<int>(accessors.size() - 1);
        };
        const std::vector<float> positions = { 0,0,0, 1,0,0, 0,1,0, 1,1,0, 0,2,0, 1,2,0 };
        const std::vector<float> normals = { 0,0,1, 0,0,1, 0,0,1, 0,0,1, 0,0,1, 0,0,1 };
        const std::vector<float> uvs = { 0,0, 1,0, 0,0.5f, 1,0.5f, 0,1, 1,1 };
        const std::vector<uint8_t> joints = { 0,0,0,0, 0,0,0,0, 0,1,0,0, 0,1,0,0, 1,0,0,0, 1,0,0,0 };
        const std::vector<float> weights = { 1,0,0,0, 1,0,0,0, 0.5f,0.5f,0,0, 0.5f,0.5f,0,0, 1,0,0,0, 1,0,0,0 };
        const std::vector<uint16_t> indices = { 0,1,2, 2,1,3, 2,3,4, 4,3,5 };
        const std::vector<float> inverseBind = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,   1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1 };
        const std::vector<float> times = { 0.0f, 1.0f };
        const std::vector<float> rotations = { 0,0,0,1, 0,0,0.70710678f,0.70710678f };
        const int position = add(Push(buffer, positions), positions.size() * 4, 5126, 6, "VEC3",
            { { "min", { 0, 0, 0 } }, { "max", { 1, 2, 0 } } });
        const int normal = add(Push(buffer, normals), normals.size() * 4, 5126, 6, "VEC3");
        const int uv = add(Push(buffer, uvs), uvs.size() * 4, 5126, 6, "VEC2");
        const int joint = add(Push(buffer, joints), joints.size(), 5121, 6, "VEC4");
        const int weight = add(Push(buffer, weights), weights.size() * 4, 5126, 6, "VEC4");
        const int index = add(Push(buffer, indices), indices.size() * 2, 5123, 12, "SCALAR");
        const int ibm = add(Push(buffer, inverseBind), inverseBind.size() * 4, 5126, 2, "MAT4");
        const int input = add(Push(buffer, times), times.size() * 4, 5126, 2, "SCALAR", { { "min", { 0.0 } }, { "max", { 1.0 } } });
        const int output = add(Push(buffer, rotations), rotations.size() * 4, 5126, 2, "VEC4");

        TextureBaker::Image image;
        image.Width = 3; image.Height = 2; // NPOT on purpose: the importer must make it 4x2
        image.Rgba = { 255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255, 0,255,255,255, 255,0,255,255 };
        const std::vector<uint8_t> png = TextureBaker::EncodePng(image);

        json gltf = {
            { "asset", { { "version", "2.0" } } },
            { "scene", 0 },
            { "scenes", json::array({ { { "nodes", { 0 } } } }) },
            { "nodes", json::array({
                { { "name", "Root" }, { "children", { 1, 3 } } },
                { { "name", "Bone0" }, { "children", { 2 } } },
                { { "name", "Bone1" }, { "translation", { 0, 1, 0 } } },
                { { "name", "Body" }, { "mesh", 0 }, { "skin", 0 } } }) },
            { "skins", json::array({ { { "joints", { 1, 2 } }, { "inverseBindMatrices", ibm } } }) },
            { "meshes", json::array({ { { "name", "Body" }, { "primitives", json::array({ {
                { "attributes", { { "POSITION", position }, { "NORMAL", normal }, { "TEXCOORD_0", uv }, { "JOINTS_0", joint }, { "WEIGHTS_0", weight } } },
                { "indices", index }, { "material", 0 } } }) } } }) },
            { "materials", json::array({ { { "name", "Skin" }, { "alphaMode", "MASK" }, { "alphaCutoff", 0.3 }, { "doubleSided", true },
                { "pbrMetallicRoughness", { { "baseColorFactor", { 1.0, 0.5, 0.25, 1.0 } }, { "baseColorTexture", { { "index", 0 } } } } } } }) },
            { "textures", json::array({ { { "source", 0 } } }) },
            { "images", json::array({ { { "uri", "data:image/png;base64," + Base64(png) } } }) },
            { "animations", json::array({ { { "name", "Wave" },
                { "samplers", json::array({ { { "input", input }, { "output", output }, { "interpolation", "LINEAR" } } }) },
                { "channels", json::array({ { { "sampler", 0 }, { "target", { { "node", 2 }, { "path", "rotation" } } } } }) } } }) },
            { "buffers", json::array({ { { "byteLength", buffer.size() }, { "uri", "data:application/octet-stream;base64," + Base64(buffer) } } }) },
            { "bufferViews", views },
            { "accessors", accessors },
        };
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << gltf.dump(1);
    }

    fs::path FreshDirectory(const char* name) {
        const fs::path directory = fs::temp_directory_path() / name;
        std::error_code error;
        fs::remove_all(directory, error);
        fs::create_directories(directory / "Assets", error);
        return directory / "Assets";
    }

    std::map<std::string, std::string> GuidsByKey(const ModelImportInfo& info) {
        std::map<std::string, std::string> guids;
        for (const ModelSubAssetInfo& child : info.Children)
            if (!child.AssetGuid.empty()) guids[child.Key] = child.AssetGuid;
        return guids;
    }
}

TEST_CASE("ModelCooker: stable sub-asset GUIDs are deterministic per model + key") {
    const std::string a = ModelCooker::StableGuid("0123456789abcdef0123456789abcdef", "mesh:Root/Body#0");
    CHECK_SOFT(IsHexGuid(a), "stable GUID is 32 hex characters like every .meta GUID");
    CHECK_SOFT(a == ModelCooker::StableGuid("0123456789abcdef0123456789abcdef", "mesh:Root/Body#0"), "same model + key -> same GUID");
    CHECK_SOFT(a != ModelCooker::StableGuid("0123456789abcdef0123456789abcdef", "mesh:Root/Body#1"), "another key -> another GUID");
    CHECK_SOFT(a != ModelCooker::StableGuid("ffffffffffffffffffffffffffffffff", "mesh:Root/Body#0"), "another model -> another GUID");
}

TEST_CASE("ModelCooker: bone palettes respect the PICA200 24-bone budget and weights are clamped + normalized") {
    const ImportedMesh mesh = MakeSkinnedStrip(60, 70);
    ModelCooker::MeshCookOptions options;
    options.MaxBonesPerVertex = 4;
    const ModelCooker::CookedMesh cooked = ModelCooker::CookMesh(mesh, options);
    CHECK_SOFT(cooked.Triangles == 120, "every triangle is kept");
    CHECK_SOFT(cooked.SubMeshes.size() > 1, "70 bones need several palettes");
    bool palettesOk = true, weightsOk = true, indicesOk = true;
    std::set<uint16_t> bonesUsed;
    for (const auto& subMesh : cooked.SubMeshes) {
        palettesOk &= subMesh.Palette.size() <= DMeshMaxPaletteBones;
        for (uint16_t bone : subMesh.Palette) bonesUsed.insert(bone);
        for (const MeshVertex& vertex : subMesh.Vertices) {
            const float sum = vertex.BoneWeights.x + vertex.BoneWeights.y + vertex.BoneWeights.z + vertex.BoneWeights.w;
            weightsOk &= std::fabs(sum - 1.0f) < 1e-4f;
            for (int i = 0; i < 4; ++i)
                indicesOk &= vertex.BoneWeights[i] == 0.0f || vertex.BoneIndices[i] < subMesh.Palette.size();
        }
    }
    CHECK_SOFT(palettesOk && cooked.MaxPaletteSize <= DMeshMaxPaletteBones, "no draw references more than 24 bones");
    CHECK_SOFT(weightsOk, "kept weights are renormalized to 1 (the shaders blend them as-is)");
    CHECK_SOFT(indicesOk, "vertex bone ids are palette-local");
    CHECK_SOFT(cooked.ClampedInfluenceVertices > cooked.SourceVertices / 2 && cooked.ClampedInfluenceVertices <= cooked.SourceVertices,
        "vertices with more than 4 influences were reduced to 4 and counted");

    ModelCooker::MeshCookOptions two = options;
    two.MaxBonesPerVertex = 2;
    const ModelCooker::CookedMesh clamped = ModelCooker::CookMesh(mesh, two);
    bool twoOk = true;
    for (const auto& subMesh : clamped.SubMeshes)
        for (const MeshVertex& vertex : subMesh.Vertices) twoOk &= vertex.BoneWeights.z == 0.0f && vertex.BoneWeights.w == 0.0f;
    CHECK_SOFT(twoOk, "Max Bones/Vertex = 2 keeps two influences");

    ModelCooker::MeshCookOptions noRig = options;
    noRig.Skinned = false;
    const ModelCooker::CookedMesh rigless = ModelCooker::CookMesh(mesh, noRig);
    CHECK_SOFT(rigless.Bones.empty() && rigless.SubMeshes.size() == 1, "Rig None cooks one static bind-pose draw");
}

TEST_CASE("ModelCooker: large meshes split under the u16 vertex limit and survive .dmesh v4 (float + quantized)") {
    ImportedMesh grid;
    grid.Name = "Grid";
    const int size = 40; // 41 x 41 = 1681 vertices, 3200 triangles
    for (int y = 0; y <= size; ++y)
        for (int x = 0; x <= size; ++x) {
            grid.Positions.push_back({ x * 0.5f, std::sin(x * 0.3f) * 0.25f, y * 0.5f - 3.0f });
            grid.Normals.push_back({ 0.0f, 1.0f, 0.0f });
            grid.UVs.push_back({ x / 8.0f, y / 8.0f }); // tiling UVs outside [0,1]
        }
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const uint32_t a = y * (size + 1) + x, b = a + 1, c = a + size + 1, d = c + 1;
            grid.Indices.insert(grid.Indices.end(), { a, c, b, b, c, d });
        }
    ModelCooker::MeshCookOptions options;
    options.MaxVerticesPerSubMesh = 300;
    const ModelCooker::CookedMesh cooked = ModelCooker::CookMesh(grid, options);
    bool limitOk = true;
    for (const auto& subMesh : cooked.SubMeshes) limitOk &= subMesh.Vertices.size() <= 300;
    CHECK_SOFT(cooked.SubMeshes.size() >= 6 && limitOk, "every draw stays under the vertex limit");
    CHECK_SOFT(cooked.Triangles == 3200 && cooked.IndexCount() == 9600, "splitting keeps every triangle");

    for (bool quantize : { false, true }) {
        ModelCooker::DMeshWriteOptions write;
        write.Quantize = quantize;
        const std::vector<uint8_t> bytes = ModelCooker::EncodeDMesh(cooked, write);
        MeshData data;
        std::string error;
        CHECK(MeshLoader::ParseDMesh(bytes.data(), bytes.size(), data, &error));
        CHECK_SOFT(data.SourceVersion == 4, "written as .dmesh v4");
        CHECK_SOFT(data.Vertices.size() == 9600 && data.SubMeshes.size() == cooked.SubMeshes.size(), "loader expands to the flat triangle list both renderers draw");
        float maxError = 0.0f, uvError = 0.0f;
        size_t flat = 0;
        for (const auto& subMesh : cooked.SubMeshes)
            for (uint16_t index : subMesh.Indices) {
                maxError = std::max(maxError, glm::length(data.Vertices[flat].Position - subMesh.Vertices[index].Position));
                uvError = std::max(uvError, glm::length(data.Vertices[flat].TexCoord - subMesh.Vertices[index].TexCoord));
                ++flat;
            }
        CHECK_SOFT(maxError < (quantize ? 0.001f : 1e-6f), quantize ? "16-bit positions stay within 1 mm on a 20 m mesh" : "float positions are exact");
        CHECK_SOFT(uvError < (quantize ? 0.001f : 1e-6f), "UVs (including tiling > 1) survive");
        if (quantize) {
            ModelCooker::DMeshWriteOptions plain;
            CHECK_SOFT(bytes.size() * 2 < ModelCooker::EncodeDMesh(cooked, plain).size(), "Mesh Compression more than halves the file");
        }
    }
    // Corrupt / truncated data is rejected instead of read out of bounds.
    ModelCooker::DMeshWriteOptions write;
    std::vector<uint8_t> bytes = ModelCooker::EncodeDMesh(cooked, write);
    bytes.resize(bytes.size() / 2);
    MeshData broken;
    CHECK_SOFT(!MeshLoader::ParseDMesh(bytes.data(), bytes.size(), broken) && broken.Vertices.empty(), "truncated v4 file is rejected");
}

TEST_CASE("ModelCooker: quantized skin weights still sum to one") {
    const ImportedMesh mesh = MakeSkinnedStrip(8, 6);
    const ModelCooker::CookedMesh cooked = ModelCooker::CookMesh(mesh, {});
    ModelCooker::DMeshWriteOptions write;
    write.Quantize = true;
    const std::vector<uint8_t> bytes = ModelCooker::EncodeDMesh(cooked, write);
    MeshData data;
    CHECK(MeshLoader::ParseDMesh(bytes.data(), bytes.size(), data));
    bool ok = !data.Vertices.empty();
    for (const MeshVertex& vertex : data.Vertices) {
        const float sum = vertex.BoneWeights.x + vertex.BoneWeights.y + vertex.BoneWeights.z + vertex.BoneWeights.w;
        ok &= std::fabs(sum - 1.0f) < 1e-4f;
    }
    CHECK_SOFT(ok, "8-bit weights are renormalized on load");
    CHECK_SOFT(data.Bones.size() == 6 && !data.SubMeshes.front().BonePalette.empty(), "bones + palettes are kept");
}

TEST_CASE("ModelCooker: clips are sampled at the sample rate, trimmed to their range and keyframe-reduced") {
    ImportedAnimation take;
    take.Name = "Take";
    take.Duration = 2.0;
    ImportedChannel linear;
    linear.Node = "Hips";
    linear.Positions = { { 0.0, { 0, 0, 0 } }, { 2.0, { 4, 2, 0 } } };
    linear.Rotations = { { 0.0, glm::quat(1, 0, 0, 0) }, { 2.0, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)) } };
    ImportedChannel wave;
    wave.Node = "Arm";
    for (int i = 0; i <= 40; ++i)
        wave.Positions.push_back({ i * 0.05, { 0.0f, std::sin(i * 0.05f * 6.2831853f), 0.0f } });
    ImportedChannel still;
    still.Node = "Head";
    still.Scales = { { 0.0, { 1, 1, 1 } }, { 1.0, { 1, 1, 1 } }, { 2.0, { 1, 1, 1 } } };
    take.Channels = { linear, wave, still };

    ModelClipSettings clip{ "Run", "Take", 0.0f, -1.0f, true };
    ModelCooker::ClipBakeOptions options;
    options.SampleRate = 30.0f;
    options.Scale = 0.5f;
    options.KeyframeReduction = false;
    const AnimationClipData raw = ModelCooker::BakeClip(take, clip, options);
    CHECK_SOFT(std::fabs(raw.Duration - 2.0f) < 1e-5f && raw.Loop && raw.SampleRate == 30.0f, "clip keeps duration, loop and rate");
    CHECK_SOFT(raw.Channels.size() == 3 && raw.Channels[0].PositionKeys.size() == 61, "2 s at 30 fps = 61 samples (both ends)");
    CHECK_SOFT(std::fabs(raw.Channels[0].PositionKeys.back().Value.x - 2.0f) < 1e-5f, "position keys get the model scale");

    options.KeyframeReduction = true;
    const AnimationClipData reduced = ModelCooker::BakeClip(take, clip, options);
    CHECK_SOFT(reduced.Channels[0].PositionKeys.size() == 2, "linear motion reduces to its two end keys");
    CHECK_SOFT(reduced.Channels[0].RotationKeys.size() == 2, "a single slerp reduces to two keys");
    CHECK_SOFT(reduced.Channels[2].ScaleKeys.size() == 1, "a constant curve collapses to one key");
    const size_t waveKeys = reduced.Channels[1].PositionKeys.size();
    CHECK_SOFT(waveKeys > 4 && waveKeys < 61, "a sine keeps the keys it needs, drops the rest");
    float worst = 0.0f;
    for (const AnimationVec3Key& key : raw.Channels[1].PositionKeys)
        worst = std::max(worst, std::fabs(AnimationClipLoader::Sample(reduced.Channels[1].PositionKeys, key.Time, {}).y - key.Value.y));
    // Scaled sine spans 1.0 (+-0.5): 0.5 % of that range.
    CHECK_SOFT(worst <= 1.0f * 0.005f + 1e-5f, "reduced curve stays within 0.5% of its range of the baked samples");

    ModelClipSettings part{ "Second", "Take", 1.0f, 1.5f, false };
    const AnimationClipData trimmed = ModelCooker::BakeClip(take, part, options);
    CHECK_SOFT(std::fabs(trimmed.Duration - 0.5f) < 1e-5f && !trimmed.Loop, "clip range sets the duration");
    CHECK_SOFT(std::fabs(trimmed.Channels[0].PositionKeys.front().Value.x - 1.0f) < 1e-4f && trimmed.Channels[0].PositionKeys.front().Time == 0.0f,
        "clip starts at its Start time, re-based to 0");

    ModelCooker::ClipBakeOptions inPlace = options;
    inPlace.RemoveRootMotion = true;
    inPlace.RootMotionNode = "Hips";
    const AnimationClipData locked = ModelCooker::BakeClip(take, clip, inPlace);
    bool lockedOk = true;
    for (const AnimationVec3Key& key : locked.Channels[0].PositionKeys) lockedOk &= key.Value.x == 0.0f && key.Value.z == 0.0f;
    CHECK_SOFT(lockedOk && std::fabs(AnimationClipLoader::Sample(locked.Channels[0].PositionKeys, 2.0f, {}).y - 1.0f) < 1e-4f,
        "Remove Root Motion locks XZ and keeps the vertical motion");

    AnimationClipData parsed;
    CHECK_SOFT(AnimationClipLoader::ParseJson(AnimationClipLoader::ToJson(reduced), parsed) && parsed.Loop && parsed.Channels.size() == 3,
        "baked clip (with Loop) round-trips through the .anim JSON the 3DS loads");
}

TEST_CASE("ModelCooker: material mapping (Lit by default, alpha / cull / specular / unlit)") {
    ImportedMaterial source;
    source.BaseColor = { 0.5f, 0.25f, 1.0f, 1.0f };
    source.Emissive = { 2.0f, 0.1f, 0.0f };
    Material lit = ModelCooker::MapMaterial(source, "aaaabbbbccccddddaaaabbbbccccdddd", "");
    CHECK_SOFT(lit.Shader == BuiltinShader::LitDiffuse && lit.Cull == CullMode::Back && lit.EffectiveSurface() == SurfaceType::Opaque, "default is Lit/Diffuse, back-face culled, opaque");
    CHECK_SOFT(lit.Texture.Guid == "aaaabbbbccccddddaaaabbbbccccdddd" && lit.Color.g == 0.25f, "base color + texture mapped");
    CHECK_SOFT(lit.EmissionColor.r == 1.0f && lit.EmissionColor.g == 0.1f, "emission clamped to the PICA range");
    source.AlphaMode = ImportedAlphaMode::Mask;
    source.AlphaCutoff = 0.3f;
    source.TwoSided = true;
    Material cutout = ModelCooker::MapMaterial(source, "", "");
    CHECK_SOFT(cutout.EffectiveSurface() == SurfaceType::Cutout && std::fabs(cutout.Cutoff - 0.3f) < 1e-6f && cutout.Cull == CullMode::Off, "glTF MASK -> Cutout, double sided -> Cull Off");
    source.AlphaMode = ImportedAlphaMode::Opaque;
    source.Opacity = 0.5f;
    CHECK_SOFT(ModelCooker::MapMaterial(source, "", "").EffectiveSurface() == SurfaceType::Transparent, "opacity < 1 -> Transparent");
    source.Opacity = 1.0f;
    source.Specular = { 0.8f, 0.8f, 0.8f };
    source.Shininess = 64.0f;
    const Material specular = ModelCooker::MapMaterial(source, "", "");
    CHECK_SOFT(specular.Shader == BuiltinShader::LitSpecular && std::fabs(specular.Shininess - 0.5f) < 1e-5f, "real specular -> Lit/Specular");
    source.Unlit = true;
    CHECK_SOFT(ModelCooker::MapMaterial(source, "", "").Shader == BuiltinShader::UnlitTexture, "unlit materials stay unlit");
}

TEST_CASE("TextureBaker: power-of-two resize and the PNG encoder round trip through stb_image") {
    CHECK_SOFT(TextureBaker::NearestPowerOfTwo(600, 1024) == 512 && TextureBaker::NearestPowerOfTwo(900, 1024) == 1024 &&
        TextureBaker::NearestPowerOfTwo(2000, 512) == 512 && TextureBaker::NearestPowerOfTwo(3, 1024) == 4, "nearest power of two, clamped to the max size");
    TextureBaker::Image image;
    image.Width = 37; image.Height = 19;
    for (int i = 0; i < image.Width * image.Height; ++i)
        image.Rgba.insert(image.Rgba.end(), { static_cast<uint8_t>(i * 7), static_cast<uint8_t>(i / 3), 200, static_cast<uint8_t>(i % 2 ? 255 : 128) });
    const std::vector<uint8_t> png = TextureBaker::EncodePng(image);
    TextureBaker::Image decoded;
    CHECK(TextureBaker::DecodeMemory(png, decoded));
    CHECK_SOFT(decoded.Width == 37 && decoded.Height == 19 && decoded.Rgba == image.Rgba, "PNG encoding is lossless");
    const TextureBaker::Image resized = TextureBaker::ResizeToPowerOfTwo(image, 16);
    CHECK_SOFT(resized.Width == 16 && resized.Height == 16, "Max Texture Size clamps and resizes to a power of two");
    CHECK_SOFT(ModelCooker::N3DSTextureBytes(256, 256, "ETC1", false) == 32768 && ModelCooker::N3DSTextureBytes(256, 256, "Auto", false) == 262144,
        "3DS texture memory per format");
}

TEST_CASE("ModelImporter: a glTF is baked into mesh / material / texture / clip / prefab with GUIDs stable across re-imports") {
    const fs::path assets = FreshDirectory("duality_model_import_test");
    const fs::path model = assets / "Hero.gltf";
    WriteSkinnedGltf(model);
    AssetDatabase::Refresh(assets.string());

    CHECK(ModelImporter::ImportNow(model, true));
    const ModelImportInfo first = ModelImporter::Inspect(model);
    int meshes = 0, materials = 0, textures = 0, clips = 0, avatars = 0;
    std::string meshPath, materialPath, texturePath, clipPath;
    for (const ModelSubAssetInfo& child : first.Children) {
        if (child.Type == "Mesh") { ++meshes; meshPath = child.AssetPath; }
        if (child.Type == "Material") { ++materials; materialPath = child.AssetPath; }
        if (child.Type == "Texture") { ++textures; texturePath = child.AssetPath; }
        if (child.Type == "Animation") { ++clips; clipPath = child.AssetPath; }
        if (child.Type == "Avatar") ++avatars;
        if (!child.AssetGuid.empty() && child.Type != "Avatar")
            CHECK_SOFT(AssetDatabase::ResolvePath(child.AssetGuid) == child.AssetPath, "every generated sub-asset is registered by GUID");
    }
    CHECK_SOFT(meshes == 1 && materials == 1 && textures == 1 && clips == 1 && avatars == 1, "Project window children: Mesh, Material, Texture, Animation, Avatar");

    const MeshData& mesh = MeshLoader::Load(meshPath);
    CHECK_SOFT(mesh.SourceVersion == DMeshCurrentVersion && mesh.Vertices.size() == 12 && mesh.Bones.size() == 2, "cooked .dmesh v4 with its two bones");
    CHECK_SOFT(!mesh.SubMeshes.empty() && mesh.SubMeshes.front().BonePalette.size() == 2, "skinned draw carries its bone palette");

    const Material material = MaterialLoader::Load(materialPath);
    CHECK_SOFT(material.EffectiveSurface() == SurfaceType::Cutout && material.Cull == CullMode::Off && !material.Texture.Guid.empty() &&
        std::fabs(material.Color.g - 0.5f) < 1e-4f, "glTF material mapped (MASK, double sided, base color + embedded texture)");
    TextureBaker::Image texture;
    CHECK_SOFT(TextureBaker::DecodeFile(texturePath, texture) && texture.Width == 4 && texture.Height == 2, "3x2 embedded texture extracted as a 4x2 power-of-two PNG");
    const TextureImportSettings textureSettings = TextureImportSettings::Load(texturePath);
    CHECK_SOFT(textureSettings.GenerateMipmaps && textureSettings.WrapMode == TextureWrapMode::Repeat, "model textures get 3D sampling defaults");

    const AnimationClipData& clip = AnimationClipLoader::Load(clipPath);
    const AnimationChannel* bone = AnimationClipLoader::FindChannel(clip, "Bone1");
    CHECK_SOFT(std::fabs(clip.Duration - 1.0f) < 1e-4f && clip.SampleRate == 30.0f && clip.Loop, "clip baked at 30 fps, looping by default");
    CHECK_SOFT(bone && bone->RotationKeys.size() == 2, "baked rotation keys reduced back to the two source keys");

    std::ifstream prefabFile(first.PrefabPath);
    const json prefab = json::parse(prefabFile, nullptr, false);
    bool skinnedFound = false, animationFound = false;
    std::set<std::string> names;
    for (const json& entity : prefab.value("Entities", json::array())) {
        if (entity.contains("Name")) names.insert(entity["Name"].value("Name", std::string()));
        if (entity.contains("Skinned Mesh Renderer"))
            skinnedFound = entity["Skinned Mesh Renderer"].value("Bones", json::array()).size() == 2;
        animationFound |= entity.contains("Animation");
    }
    CHECK_SOFT(prefab.is_object() && names.count("Hero") && names.count("Bone0") && names.count("Bone1") && names.count("Body"), "model prefab: one entity per node, root named after the file");
    CHECK_SOFT(skinnedFound && animationFound, "prefab has a Skinned Mesh Renderer bound to both bones and an Animation on the root");
    CHECK_SOFT(ModelAsset::ResolveInstantiablePrefab(AssetMeta::EnsureMetaFile(model)) == first.PrefabPath, "the model GUID resolves to its prefab");

    // Forced re-import, then changed settings: same GUIDs.
    const auto guids = GuidsByKey(first);
    CHECK(ModelImporter::ImportNow(model, true));
    CHECK_SOFT(GuidsByKey(ModelImporter::Inspect(model)) == guids, "forced re-import keeps every sub-asset GUID");
    ModelImportSettings settings = ModelImportSettings::Load(model.string());
    settings.ScaleFactor = 2.0f;
    settings.MeshCompression = ModelMeshCompression::On;
    settings.MaxBonesPerVertex = 2;
    ModelImportSettings::Save(model.string(), settings);
    CHECK(ModelImporter::ImportNow(model));
    CHECK_SOFT(GuidsByKey(ModelImporter::Inspect(model)) == guids, "changing import settings keeps every sub-asset GUID");
    CHECK_SOFT(std::fabs(MeshLoader::Load(meshPath).BoundsMax.y - 4.0f) < 0.01f, "Scale Factor is baked into the re-imported mesh (and its cache was refreshed)");

    // The generated folder is like Unity's Library: deleting it (and even the inventory) rebuilds
    // the same GUIDs, because they derive from the model GUID + sub-asset key.
    std::error_code error;
    fs::remove_all(assets / ".duality-import", error);
    std::ifstream metaIn(model.string() + ".meta");
    json meta = json::parse(metaIn, nullptr, false);
    metaIn.close();
    meta.erase("ModelImporter");
    std::ofstream(model.string() + ".meta", std::ios::trunc) << meta.dump(2);
    CHECK(ModelImporter::ImportNow(model));
    CHECK_SOFT(GuidsByKey(ModelImporter::Inspect(model)) == guids, "deleting .duality-import and the inventory regenerates identical GUIDs");

    // Unchanged source + settings: no re-import (up-to-date check by content hash).
    std::error_code timeError;
    const auto meshTime = fs::last_write_time(meshPath, timeError);
    CHECK(ModelImporter::ImportNow(model, false));
    CHECK_SOFT(fs::last_write_time(meshPath, timeError) == meshTime, "an up-to-date model is not re-cooked");
}

TEST_CASE("Scene world transforms compose in 3D for imported hierarchies (2D stays bit-identical)") {
    Scene scene;
    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, parent, {}, false);
    parent.GetComponent<TransformComponent>().Rotation = { 0.0f, 90.0f, 0.0f };
    parent.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    child.GetComponent<TransformComponent>().Translation = { 1.0f, 0.0f, 0.0f };
    child.GetComponent<TransformComponent>().Rotation = { 90.0f, 0.0f, 0.0f };
    const TransformComponent world = scene.GetWorldTransform(child);
    CHECK_SOFT(glm::length(world.Translation - glm::vec3(1.0f, 2.0f, 2.0f)) < 1e-4f, "a Y-rotated parent rotates the child's offset (+X -> -Z)");
    const glm::quat expected = glm::quat(glm::radians(glm::vec3(0, 90, 0))) * glm::quat(glm::radians(glm::vec3(90, 0, 0)));
    CHECK_SOFT(std::fabs(std::fabs(glm::dot(glm::quat(glm::radians(world.Rotation)), expected)) - 1.0f) < 1e-4f, "rotations compose as quaternions");

    // Reparenting with "keep world position" inverts the same composition.
    Entity other = scene.CreateEntity("Other");
    other.GetComponent<TransformComponent>().Rotation = { 30.0f, -45.0f, 10.0f };
    other.GetComponent<TransformComponent>().Translation = { -2.0f, 0.5f, 4.0f };
    scene.SetParent(child, other, {}, true);
    const TransformComponent kept = scene.GetWorldTransform(child);
    CHECK_SOFT(glm::length(kept.Translation - world.Translation) < 1e-3f &&
        std::fabs(std::fabs(glm::dot(glm::quat(glm::radians(kept.Rotation)), glm::quat(glm::radians(world.Rotation)))) - 1.0f) < 1e-4f,
        "preserveWorldPosition keeps the 3D world pose");

    Entity flatParent = scene.CreateEntity("Flat");
    Entity flatChild = scene.CreateEntity("FlatChild");
    scene.SetParent(flatChild, flatParent, {}, false);
    flatParent.GetComponent<TransformComponent>().Rotation = { 0.0f, 0.0f, 90.0f };
    flatChild.GetComponent<TransformComponent>().Translation = { 1.0f, 0.0f, 0.5f };
    const TransformComponent flat = scene.GetWorldTransform(flatChild);
    CHECK_SOFT(std::fabs(flat.Translation.x) < 1e-5f && std::fabs(flat.Translation.y - 1.0f) < 1e-5f && flat.Translation.z == 0.5f &&
        flat.Rotation == glm::vec3(0.0f, 0.0f, 90.0f), "2D (Z-only) hierarchies use the unchanged planar formula");
}

TEST_CASE("ModelImporter: first-importer (.meta inventory v1) GUIDs survive the upgrade") {
    const fs::path assets = FreshDirectory("duality_model_import_upgrade_test");
    const fs::path model = assets / "Legacy.gltf";
    WriteSkinnedGltf(model);
    const std::string legacyMesh = "11112222333344445555666677778888";
    const std::string legacyClip = "9999aaaabbbbccccddddeeeeffff0000";
    json meta = { { "guid", "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f" }, { "ModelImporter", { { "Version", 1 }, { "Children", json::array({
        { { "Type", "Mesh" }, { "Name", "Body" }, { "Index", 0 }, { "AssetGuid", legacyMesh }, { "AssetPath", "x/mesh_0.dmesh" } },
        { { "Type", "Animation" }, { "Name", "Wave" }, { "Index", 0 }, { "AssetGuid", legacyClip }, { "AssetPath", "x/animation_0.anim" } } }) } } } };
    std::ofstream(model.string() + ".meta", std::ios::trunc) << meta.dump(2);
    CHECK(ModelImporter::ImportNow(model));
    std::string mesh, clip;
    for (const ModelSubAssetInfo& child : ModelImporter::Inspect(model).Children) {
        if (child.Type == "Mesh") mesh = child.AssetGuid;
        if (child.Type == "Animation") clip = child.AssetGuid;
    }
    CHECK_SOFT(mesh == legacyMesh && clip == legacyClip, "scene references made with the first importer keep resolving");
    CHECK_SOFT(fs::exists(ModelAsset::ImportDirectory(model.string(), "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f")), "outputs live under .duality-import/<model guid>");
}
