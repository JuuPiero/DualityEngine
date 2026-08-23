#include "DualityEngine/Asset/MeshLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace Duality {

    namespace {
        std::unordered_map<std::string, MeshData> s_Cache;

        // Parses one OBJ face-vertex token ("3", "3/2", "3//1", or "3/2/1") into its 1-based
        // position/texcoord indices (0 = not present). Negative (relative-to-end) OBJ indices
        // are not supported -- a known gap of this hand-rolled parser.
        void ParseFaceVertex(char* token, int& posIndex, int& texIndex) {
            int values[3] = { 0, 0, 0 };
            char* segment = std::strtok(token, "/");
            for (int slot = 0; slot < 3 && segment; slot++) {
                values[slot] = std::atoi(segment);
                // strtok on "3//1" skips the empty middle segment entirely (no token emitted
                // for it), so re-tokenizing with nullptr here would misalign vt into vn's slot
                // -- instead this parser only supports the two OBJ shapes it actually needs
                // ("v", "v/vt", "v/vt/vn"), not "v//vn" (a mesh with normals but no UVs, which
                // this unlit importer wouldn't use the normals from anyway).
                segment = std::strtok(nullptr, "/");
            }
            posIndex = values[0];
            texIndex = values[1];
        }
    }

    const MeshData& MeshLoader::Load(const std::string& path) {
        auto cached = s_Cache.find(path);
        if (cached != s_Cache.end())
            return cached->second;

        MeshData data;
        FILE* file = std::fopen(path.c_str(), "rb");
        if (file) {
            std::vector<glm::vec3> positions;
            std::vector<glm::vec2> texcoords;

            char line[512];
            while (std::fgets(line, sizeof(line), file)) {
                if (line[0] == 'v' && line[1] == ' ') {
                    glm::vec3 v{};
                    std::sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z);
                    positions.push_back(v);
                } else if (line[0] == 'v' && line[1] == 't') {
                    glm::vec2 vt{};
                    std::sscanf(line + 3, "%f %f", &vt.x, &vt.y);
                    texcoords.push_back(vt);
                } else if (line[0] == 'f' && line[1] == ' ') {
                    // Fan-triangulates faces with more than 3 vertices (convex n-gons only).
                    std::vector<std::pair<int, int>> faceVerts;
                    char* token = std::strtok(line + 2, " \t\r\n");
                    while (token) {
                        int posIndex = 0, texIndex = 0;
                        ParseFaceVertex(token, posIndex, texIndex);
                        if (posIndex > 0)
                            faceVerts.emplace_back(posIndex, texIndex);
                        token = std::strtok(nullptr, " \t\r\n");
                    }

                    auto emit = [&](const std::pair<int, int>& faceVertex) {
                        glm::vec3 pos = (faceVertex.first >= 1 && faceVertex.first <= static_cast<int>(positions.size()))
                            ? positions[faceVertex.first - 1] : glm::vec3(0.0f);
                        glm::vec2 tex = (faceVertex.second >= 1 && faceVertex.second <= static_cast<int>(texcoords.size()))
                            ? texcoords[faceVertex.second - 1] : glm::vec2(0.0f);
                        data.Vertices.push_back({ pos, tex });
                    };
                    for (size_t i = 1; i + 1 < faceVerts.size(); i++) {
                        emit(faceVerts[0]);
                        emit(faceVerts[i]);
                        emit(faceVerts[i + 1]);
                    }
                }
            }
            std::fclose(file);

            float maxDistanceSq = 0.0f;
            for (const MeshVertex& vertex : data.Vertices)
                maxDistanceSq = std::max(maxDistanceSq, glm::dot(vertex.Position, vertex.Position));
            data.BoundingRadius = std::sqrt(maxDistanceSq);
        }

        return s_Cache.emplace(path, std::move(data)).first->second;
    }

}
