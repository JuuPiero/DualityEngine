// MeshLoader (OBJ import) regression coverage -- specifically the quad-face/no-UV parsing bug
// found and fixed this pass: nested std::strtok calls (splitting "1/1/1" inside the outer
// per-face-vertex token loop) corrupted the outer loop's own strtok state, so only the FIRST
// vertex of any face with more than 3 vertices ever got parsed. Any all-quad mesh (e.g.
// Blender's own unmodified cube export) therefore produced ZERO triangles -- exactly "drag an
// .obj onto Mesh, nothing renders".
#include "TestFramework.h"

#include <cstdio>
#include <filesystem>

#include "DualityEngine/Asset/MeshLoader.h"

using namespace Duality;

namespace {
    std::string WriteTempObj(const char* filename, const char* contents) {
        std::string path = (std::filesystem::temp_directory_path() / filename).string();
        FILE* file = std::fopen(path.c_str(), "wb");
        std::fputs(contents, file);
        std::fclose(file);
        return path;
    }
}

TEST_CASE("MeshLoader triangulates a quad-faced, no-UV (\"v//vn\") mesh correctly") {
    // A unit cube, exactly the shape Blender exports by default for an un-UV-unwrapped mesh:
    // quad faces, "v//vn" face-vertex tokens (position + normal, no texcoord).
    const char* obj =
        "v -1 -1 -1\nv -1 -1 1\nv -1 1 -1\nv -1 1 1\nv 1 -1 -1\nv 1 -1 1\nv 1 1 -1\nv 1 1 1\n"
        "vn -1 0 0\nvn 0 0 1\nvn 1 0 0\nvn 0 0 -1\nvn 0 -1 0\nvn 0 1 0\n"
        "f 1//1 2//1 4//1 3//1\n"
        "f 6//2 8//2 4//2 2//2\n"
        "f 5//3 7//3 8//3 6//3\n"
        "f 1//4 3//4 7//4 5//4\n"
        "f 1//5 5//5 6//5 2//5\n"
        "f 3//6 4//6 8//6 7//6\n";
    std::string path = WriteTempObj("duality_engine_test_quad_cube.obj", obj);

    const MeshData& data = MeshLoader::Load(path);
    // 6 quad faces, fan-triangulated into 2 triangles (3 verts) each = 36 flat vertices.
    CHECK_SOFT(data.Vertices.size() == 36, "all 6 quad faces contributed triangles, not just the first vertex of each");
    CHECK_SOFT(data.BoundingRadius > 1.5f && data.BoundingRadius < 2.0f, "bounding radius matches a unit cube's corner distance (sqrt(3))");

    std::filesystem::remove(path);
}

TEST_CASE("MeshLoader still handles a plain triangle-only, textured mesh (\"v/vt\")") {
    const char* obj =
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "f 1/1 2/2 3/3\n";
    std::string path = WriteTempObj("duality_engine_test_triangle.obj", obj);

    const MeshData& data = MeshLoader::Load(path);
    CHECK_SOFT(data.Vertices.size() == 3, "a single triangle face produces exactly 3 vertices");
    if (data.Vertices.size() == 3)
        CHECK_SOFT(data.Vertices[1].TexCoord.x == 1.0f, "texcoord indices still resolve correctly for v/vt faces");

    std::filesystem::remove(path);
}

TEST_CASE("MeshLoader returns empty Vertices for a nonexistent file") {
    const MeshData& data = MeshLoader::Load("this_mesh_does_not_exist.obj");
    CHECK_SOFT(data.Vertices.empty(), "missing file falls back to an empty mesh, not a crash");
}
