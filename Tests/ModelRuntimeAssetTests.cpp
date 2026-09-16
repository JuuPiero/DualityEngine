#include "TestFramework.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/Asset/MeshLoader.h"

using namespace Duality;

TEST_CASE("MeshLoader reads the cooked Assimp-free dmesh runtime format") {
    const std::string path = (std::filesystem::temp_directory_path() / "duality_engine_test.dmesh").string();
    std::ofstream output(path, std::ios::binary);
    const char magic[] = { 'D', 'M', 'S', 'H' };
    const uint32_t version = 1, vertices = 3, subMeshes = 1, firstVertex = 0;
    const float radius = 1.0f;
    output.write(magic, 4); output.write(reinterpret_cast<const char*>(&version), 4);
    output.write(reinterpret_cast<const char*>(&vertices), 4); output.write(reinterpret_cast<const char*>(&subMeshes), 4);
    output.write(reinterpret_cast<const char*>(&radius), 4);
    const float triangle[][12] = {
        { 0,0,0, 0,0, 0,0,1, 1,0,0,1 }, { 1,0,0, 1,0, 0,0,1, 0,1,0,1 }, { 0,1,0, 0,1, 0,0,1, 0,0,1,1 }
    };
    output.write(reinterpret_cast<const char*>(triangle), sizeof(triangle));
    output.write(reinterpret_cast<const char*>(&firstVertex), 4); output.write(reinterpret_cast<const char*>(&vertices), 4);
    output.close();

    const MeshData& mesh = MeshLoader::Load(path);
    CHECK_SOFT(mesh.Vertices.size() == 3, "cooked mesh preserves its triangle vertices");
    CHECK_SOFT(mesh.SubMeshes.size() == 1 && mesh.SubMeshes[0].VertexCount == 3, "cooked mesh preserves submesh range");
    CHECK_SOFT(mesh.Vertices.size() > 1 && mesh.Vertices[1].Color.g == 1.0f, "cooked vertex color survives binary round trip");
    std::filesystem::remove(path);
}

TEST_CASE("MeshLoader reads V3 local bone palettes for GPU skinning") {
    const std::string path = (std::filesystem::temp_directory_path() / "duality_engine_skin_palette_test.dmesh").string();
    std::ofstream output(path, std::ios::binary);
    const char magic[] = { 'D', 'M', 'S', 'H' };
    const uint32_t version = 3, vertices = 3, subMeshes = 1, bones = 2, firstVertex = 0, paletteCount = 2;
    const float radius = 1.0f;
    output.write(magic, 4); output.write(reinterpret_cast<const char*>(&version), 4);
    output.write(reinterpret_cast<const char*>(&vertices), 4); output.write(reinterpret_cast<const char*>(&subMeshes), 4);
    output.write(reinterpret_cast<const char*>(&radius), 4); output.write(reinterpret_cast<const char*>(&bones), 4);
    for (const char* name : { "Hip", "Hand" }) {
        const uint32_t length = static_cast<uint32_t>(std::strlen(name));
        const float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        output.write(reinterpret_cast<const char*>(&length), 4); output.write(name, length);
        output.write(reinterpret_cast<const char*>(identity), sizeof(identity));
    }
    const float vertex[12] = { 0,0,0, 0,0, 0,1,0, 1,1,1,1 };
    const uint8_t ids[4] = { 1,0,0,0 };
    const float weights[4] = { 0.75f, 0.25f, 0,0 };
    for (uint32_t i = 0; i < vertices; ++i) {
        output.write(reinterpret_cast<const char*>(vertex), sizeof(vertex));
        output.write(reinterpret_cast<const char*>(ids), sizeof(ids));
        output.write(reinterpret_cast<const char*>(weights), sizeof(weights));
    }
    output.write(reinterpret_cast<const char*>(&firstVertex), 4); output.write(reinterpret_cast<const char*>(&vertices), 4);
    output.write(reinterpret_cast<const char*>(&paletteCount), 4);
    const uint16_t palette[] = { 0, 1 };
    output.write(reinterpret_cast<const char*>(palette), sizeof(palette));
    output.close();

    const MeshData& mesh = MeshLoader::Load(path);
    CHECK_SOFT(mesh.Bones.size() == 2, "V3 retains the imported bone metadata");
    CHECK_SOFT(mesh.SubMeshes.size() == 1 && mesh.SubMeshes[0].BonePalette.size() == 2, "V3 retains the compact per-draw palette");
    CHECK_SOFT(mesh.Vertices.size() == 3 && mesh.Vertices[0].BoneIndices.x == 1 && mesh.Vertices[0].BoneWeights.x == 0.75f,
        "V3 keeps local byte bone indices and influence weights");
    std::filesystem::remove(path);
}

TEST_CASE("AnimationClipLoader samples transform keys from a cooked animation") {
    const std::string path = (std::filesystem::temp_directory_path() / "duality_engine_test.anim").string();
    std::ofstream output(path);
    output << R"({"Version":1,"Duration":1.0,"Channels":[{"Node":"Cube","Position":[[0,[0,0,0]],[1,[4,0,0]]],"Rotation":[],"Scale":[]}]})";
    output.close();

    const AnimationClipData& clip = AnimationClipLoader::Load(path);
    const AnimationChannel* channel = AnimationClipLoader::FindChannel(clip, "Cube");
    CHECK_SOFT(channel != nullptr, "named entity finds its imported channel");
    if (channel) CHECK_SOFT(AnimationClipLoader::Sample(channel->PositionKeys, 0.5f, {}).x == 2.0f, "position keys interpolate linearly");
    std::filesystem::remove(path);
}
