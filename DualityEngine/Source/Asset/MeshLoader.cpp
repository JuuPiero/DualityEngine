#include "DualityEngine/Asset/MeshLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>

#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        std::unordered_map<std::string, MeshData> s_Cache;

        // Parses one OBJ face-vertex token ("3", "3/2", "3//1", or "3/2/1") into its 1-based
        // position/texcoord/normal indices (0 = not present). Negative (relative-to-end) OBJ indices
        // are not supported -- a known gap of this hand-rolled parser.
        //
        // Deliberately does NOT use std::strtok here -- a real bug this fix replaces: the face
        // loop below already runs its own std::strtok(line, " \t\r\n") to split a face line into
        // vertex tokens, and strtok's internal position is ONE PIECE OF GLOBAL STATE shared by
        // every call on the thread. Calling strtok(token, "/") in here to split "1/1/1" resets
        // that same global state, so the OUTER loop's next strtok(nullptr, " \t\r\n") resumes
        // from wherever THIS call left off (already exhausted) instead of the real line --
        // confirmed empirically with a standalone repro: only the FIRST vertex of any face
        // with more than 3 vertices ever got parsed. Since the triangulation loop below needs
        // at least 3 face-vertices to emit even one triangle, ANY quad-or-larger face (the
        // default for e.g. Blender's own unmodified cube export) silently contributed zero
        // triangles -- for an all-quad mesh, that's the entire mesh, exactly the "dragged an
        // .obj onto Mesh and nothing rendered" symptom. A plain manual scan for '/' has no
        // shared state and also correctly distinguishes an EMPTY segment ("1//1", position +
        // normal, no UV -- what Blender exports for a mesh with no UV map) from a real index,
        // which the old strtok-based version also got wrong (strtok collapses "//" into a
        // single skipped delimiter, silently shifting the normal's index into the texcoord
        // slot instead of leaving it absent).
        void ParseFaceVertex(const char* token, int& posIndex, int& texIndex, int& normalIndex) {
            posIndex = std::atoi(token);
            texIndex = 0;
            normalIndex = 0;

            const char* firstSlash = std::strchr(token, '/');
            if (!firstSlash)
                return; // "v" -- position only

            const char* afterFirstSlash = firstSlash + 1;
            if (*afterFirstSlash != '/') // "v/vt" or "v/vt/vn" -- a real number follows
                texIndex = std::atoi(afterFirstSlash);

            const char* secondSlash = std::strchr(afterFirstSlash, '/');
            if (secondSlash && secondSlash[1] != '\0')
                normalIndex = std::atoi(secondSlash + 1);
            // For "v//vn", texIndex remains 0 while normalIndex is still read correctly.
        }

        struct FaceVertex {
            int PositionIndex = 0;
            int TexCoordIndex = 0;
            int NormalIndex = 0;
        };
    }

    const MeshData& MeshLoader::Load(const std::string& path) {
        auto cached = s_Cache.find(path);
        if (cached != s_Cache.end())
            return cached->second;

        MeshData data;
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) {
            Log::Warn("MeshLoader: could not open '" + path + "'");
        } else {
            std::vector<glm::vec3> positions;
            std::vector<glm::vec2> texcoords;
            std::vector<glm::vec3> normals;
            // Many DCC tools export OBJ vertex colors as extra `v x y z r g b [a]`
            // values. They are a compact baked-lighting/AO transport for static 3D
            // worlds and cost no texture fetch or additional render pass on 3DS.
            std::vector<glm::vec4> colors;

            // Vertex index (into data.Vertices) where the CURRENT part started -- closed out
            // into a real SubMesh when the next "usemtl" / "o" / "g" line is hit, or at EOF.
            // Those lines are BOUNDARY markers only. The name is never resolved (this engine
            // has no ".mtl" pipeline); the user assigns a ".mat" to each slot in the Editor.
            uint32_t subMeshStart = 0;

            char line[512];
            while (std::fgets(line, sizeof(line), file)) {
                const bool usemtl = std::strncmp(line, "usemtl", 6) == 0 && (line[6] == ' ' || line[6] == '\t');
                const bool objectGroup = (line[0] == 'o' || line[0] == 'g') && (line[1] == ' ' || line[1] == '\t');
                if (usemtl || objectGroup) {
                    // Only close out a range if it actually contains faces -- back-to-back
                    // "usemtl" lines with no faces between them (or one right at the top of the
                    // file, before any faces at all) must NOT emit an empty SubMesh entry.
                    uint32_t vertexCount = static_cast<uint32_t>(data.Vertices.size());
                    if (vertexCount > subMeshStart)
                        data.SubMeshes.push_back({ subMeshStart, vertexCount - subMeshStart });
                    subMeshStart = vertexCount;
                } else if (line[0] == 'v' && line[1] == ' ') {
                    glm::vec3 v{};
                    glm::vec4 color{ 1.0f };
                    const int valueCount = std::sscanf(line + 2, "%f %f %f %f %f %f %f", &v.x, &v.y, &v.z,
                        &color.r, &color.g, &color.b, &color.a);
                    if (valueCount == 6)
                        color.a = 1.0f;
                    // A few exporters use byte-range OBJ colors. Normalize that
                    // convention once at import rather than branching per vertex draw.
                    if (valueCount >= 6 && std::max({ color.r, color.g, color.b, color.a }) > 1.0f)
                        color /= 255.0f;
                    positions.push_back(v);
                    colors.push_back(valueCount >= 6 ? glm::clamp(color, 0.0f, 1.0f) : glm::vec4(1.0f));
                } else if (line[0] == 'v' && line[1] == 't') {
                    glm::vec2 vt{};
                    std::sscanf(line + 3, "%f %f", &vt.x, &vt.y);
                    texcoords.push_back(vt);
                } else if (line[0] == 'v' && line[1] == 'n') {
                    glm::vec3 vn{};
                    std::sscanf(line + 3, "%f %f %f", &vn.x, &vn.y, &vn.z);
                    normals.push_back(vn);
                } else if (line[0] == 'f' && line[1] == ' ') {
                    // Fan-triangulates faces with more than 3 vertices (convex n-gons only).
                    std::vector<FaceVertex> faceVerts;
                    char* token = std::strtok(line + 2, " \t\r\n");
                    while (token) {
                        int posIndex = 0, texIndex = 0, normalIndex = 0;
                        ParseFaceVertex(token, posIndex, texIndex, normalIndex);
                        if (posIndex > 0)
                            faceVerts.push_back({ posIndex, texIndex, normalIndex });
                        token = std::strtok(nullptr, " \t\r\n");
                    }

                    auto emit = [&](const FaceVertex& faceVertex) {
                        glm::vec3 pos = (faceVertex.PositionIndex >= 1 && faceVertex.PositionIndex <= static_cast<int>(positions.size()))
                            ? positions[faceVertex.PositionIndex - 1] : glm::vec3(0.0f);
                        glm::vec2 tex = (faceVertex.TexCoordIndex >= 1 && faceVertex.TexCoordIndex <= static_cast<int>(texcoords.size()))
                            ? texcoords[faceVertex.TexCoordIndex - 1] : glm::vec2(0.0f);
                        glm::vec3 normal = (faceVertex.NormalIndex >= 1 && faceVertex.NormalIndex <= static_cast<int>(normals.size()))
                            ? normals[faceVertex.NormalIndex - 1] : glm::vec3(0.0f);
                        glm::vec4 color = (faceVertex.PositionIndex >= 1 && faceVertex.PositionIndex <= static_cast<int>(colors.size()))
                            ? colors[faceVertex.PositionIndex - 1] : glm::vec4(1.0f);
                        const float normalLengthSq = glm::dot(normal, normal);
                        if (normalLengthSq > 0.000001f)
                            normal /= std::sqrt(normalLengthSq);
                        data.Vertices.push_back({ pos, tex, normal, color });
                    };
                    for (size_t i = 1; i + 1 < faceVerts.size(); i++) {
                        emit(faceVerts[0]);
                        emit(faceVerts[i]);
                        emit(faceVerts[i + 1]);
                    }
                }
            }
            std::fclose(file);

            // Close out whatever material group was still open when the file ended -- covers
            // both "at least one usemtl, and its faces run to EOF" and "no usemtl at all" (the
            // whole file is the one implicit group starting at subMeshStart == 0).
            uint32_t finalVertexCount = static_cast<uint32_t>(data.Vertices.size());
            if (finalVertexCount > subMeshStart)
                data.SubMeshes.push_back({ subMeshStart, finalVertexCount - subMeshStart });

            // Preserve valid authored vn normals.  Generate a stable flat normal only for a
            // face vertex that did not provide one, keeping old position/UV-only assets lit.
            for (size_t i = 0; i + 2 < data.Vertices.size(); i += 3) {
                MeshVertex& a = data.Vertices[i];
                MeshVertex& b = data.Vertices[i + 1];
                MeshVertex& c = data.Vertices[i + 2];
                glm::vec3 normal = glm::cross(b.Position - a.Position, c.Position - a.Position);
                float lengthSq = glm::dot(normal, normal);
                normal = lengthSq > 0.000001f ? normal / std::sqrt(lengthSq) : glm::vec3(0.0f, 1.0f, 0.0f);
                if (glm::dot(a.Normal, a.Normal) <= 0.000001f) a.Normal = normal;
                if (glm::dot(b.Normal, b.Normal) <= 0.000001f) b.Normal = normal;
                if (glm::dot(c.Normal, c.Normal) <= 0.000001f) c.Normal = normal;
            }

            if (data.Vertices.empty())
                Log::Warn("MeshLoader: '" + path + "' produced 0 vertices -- only \"v\"/\"vt\"/\"f\" lines are "
                    "supported (space-separated, positive 1-based indices), check the file uses that shape");

            float maxDistanceSq = 0.0f;
            for (const MeshVertex& vertex : data.Vertices)
                maxDistanceSq = std::max(maxDistanceSq, glm::dot(vertex.Position, vertex.Position));
            data.BoundingRadius = std::sqrt(maxDistanceSq);
        }

        return s_Cache.emplace(path, std::move(data)).first->second;
    }

}
