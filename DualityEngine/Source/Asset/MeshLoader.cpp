#include "DualityEngine/Asset/MeshLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

        // Bounds-checked little-endian cursor over an in-memory .dmesh.
        struct ByteReader {
            const uint8_t* Data = nullptr;
            size_t Size = 0;
            size_t Offset = 0;
            size_t Remaining() const { return Size - Offset; }
            bool Bytes(void* target, size_t count) {
                if (count > Remaining())
                    return false;
                if (count)
                    std::memcpy(target, Data + Offset, count);
                Offset += count;
                return true;
            }
            template<typename T> bool Scalar(T& value) { return Bytes(&value, sizeof(T)); }
            bool Floats(float* values, size_t count) { return Bytes(values, count * sizeof(float)); }
        };

        bool ReadBones(ByteReader& reader, uint32_t boneCount, MeshData& data) {
            for (uint32_t i = 0; i < boneCount; ++i) {
                uint32_t length = 0;
                if (!reader.Scalar(length) || length > 4096)
                    return false;
                MeshData::Bone bone;
                bone.Name.resize(length);
                if (length && !reader.Bytes(bone.Name.data(), length))
                    return false;
                if (!reader.Floats(&bone.Offset[0][0], 16))
                    return false;
                data.Bones.push_back(std::move(bone));
            }
            return true;
        }

        bool ReadFloatVertex(ByteReader& reader, MeshVertex& vertex) {
            float values[12]{};
            if (!reader.Floats(values, 12))
                return false;
            vertex.Position = { values[0], values[1], values[2] };
            vertex.TexCoord = { values[3], values[4] };
            vertex.Normal = { values[5], values[6], values[7] };
            vertex.Color = { values[8], values[9], values[10], values[11] };
            return true;
        }

        void ComputeBounds(MeshData& data) {
            if (data.Vertices.empty()) {
                data.BoundsMin = data.BoundsMax = glm::vec3(0.0f);
                return;
            }
            data.BoundsMin = data.BoundsMax = data.Vertices.front().Position;
            for (const MeshVertex& vertex : data.Vertices) {
                data.BoundsMin = glm::min(data.BoundsMin, vertex.Position);
                data.BoundsMax = glm::max(data.BoundsMax, vertex.Position);
            }
        }
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
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (extension == ".dmesh") {
                // Fixed little-endian layout written by ModelImporter (see MeshLoader.h).
                std::vector<uint8_t> bytes;
                if (std::fseek(file, 0, SEEK_END) == 0) {
                    const long size = std::ftell(file);
                    std::fseek(file, 0, SEEK_SET);
                    if (size > 0) {
                        bytes.resize(static_cast<size_t>(size));
                        if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
                            bytes.clear();
                    }
                }
                std::fclose(file);
                std::string error;
                if (!ParseDMesh(bytes.data(), bytes.size(), data, &error))
                    Log::Warn("MeshLoader: " + error + " .dmesh '" + path + "'");
                return s_Cache.emplace(path, std::move(data)).first->second;
            }
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

            GenerateTangents(data.Vertices);

            float maxDistanceSq = 0.0f;
            for (const MeshVertex& vertex : data.Vertices)
                maxDistanceSq = std::max(maxDistanceSq, glm::dot(vertex.Position, vertex.Position));
            data.BoundingRadius = std::sqrt(maxDistanceSq);
            ComputeBounds(data);
        }

        return s_Cache.emplace(path, std::move(data)).first->second;
    }

    void MeshLoader::Invalidate(const std::string& path) {
        if (path.empty())
            s_Cache.clear();
        else
            s_Cache.erase(path);
    }

    bool MeshLoader::ParseDMesh(const uint8_t* bytes, size_t size, MeshData& out, std::string* error) {
        out = {};
        ByteReader reader{ bytes, size };
        auto fail = [&](const char* message) {
            out = {};
            if (error) *error = message;
            return false;
        };
        char magic[4]{};
        uint32_t version = 0;
        if (!reader.Bytes(magic, 4) || std::memcmp(magic, "DMSH", 4) != 0 || !reader.Scalar(version) ||
            version < 1 || version > DMeshCurrentVersion)
            return fail("invalid");

        if (version == 4) {
            uint32_t flags = 0, vertexCount = 0, indexCount = 0, subMeshCount = 0, boneCount = 0;
            float radius = 0.0f, boundsMin[3]{}, boundsMax[3]{}, uvMin[2]{}, uvMax[2]{};
            if (!reader.Scalar(flags) || !reader.Scalar(vertexCount) || !reader.Scalar(indexCount) ||
                !reader.Scalar(subMeshCount) || !reader.Scalar(radius) || !reader.Floats(boundsMin, 3) ||
                !reader.Floats(boundsMax, 3) || !reader.Floats(uvMin, 2) || !reader.Floats(uvMax, 2) ||
                !reader.Scalar(boneCount) || boneCount > 65535)
                return fail("invalid");
            if (!ReadBones(reader, boneCount, out))
                return fail("truncated");
            const bool quantized = (flags & DMeshFlag_Quantized) != 0;
            const size_t vertexBytes = quantized ? 26u : 68u;
            if (vertexCount > reader.Remaining() / vertexBytes)
                return fail("truncated");
            std::vector<MeshVertex> unique(vertexCount);
            const glm::vec3 lo(boundsMin[0], boundsMin[1], boundsMin[2]), hi(boundsMax[0], boundsMax[1], boundsMax[2]);
            const glm::vec2 uvLo(uvMin[0], uvMin[1]), uvHi(uvMax[0], uvMax[1]);
            for (MeshVertex& vertex : unique) {
                bool ok = true;
                if (quantized) {
                    uint16_t position[3]{}, uv[2]{};
                    int8_t normal[4]{};
                    uint8_t color[4]{}, weights[4]{};
                    ok = reader.Bytes(position, sizeof(position)) && reader.Bytes(uv, sizeof(uv)) &&
                        reader.Bytes(normal, sizeof(normal)) && reader.Bytes(color, sizeof(color)) &&
                        reader.Bytes(&vertex.BoneIndices, 4) && reader.Bytes(weights, sizeof(weights));
                    for (int axis = 0; axis < 3; ++axis)
                        vertex.Position[axis] = lo[axis] + (hi[axis] - lo[axis]) * (position[axis] / 65535.0f);
                    for (int axis = 0; axis < 2; ++axis)
                        vertex.TexCoord[axis] = uvLo[axis] + (uvHi[axis] - uvLo[axis]) * (uv[axis] / 65535.0f);
                    vertex.Normal = glm::vec3(normal[0], normal[1], normal[2]) / 127.0f;
                    const float length = glm::length(vertex.Normal);
                    vertex.Normal = length > 0.0001f ? vertex.Normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
                    vertex.Color = glm::vec4(color[0], color[1], color[2], color[3]) / 255.0f;
                    float sum = 0.0f;
                    for (int influence = 0; influence < 4; ++influence) {
                        vertex.BoneWeights[influence] = weights[influence] / 255.0f;
                        sum += vertex.BoneWeights[influence];
                    }
                    if (sum > 0.0f)
                        vertex.BoneWeights /= sum;
                } else {
                    ok = ReadFloatVertex(reader, vertex) && reader.Bytes(&vertex.BoneIndices, 4) &&
                        reader.Floats(&vertex.BoneWeights[0], 4);
                }
                if (!ok)
                    return fail("truncated");
            }
            if (indexCount > reader.Remaining() / 2)
                return fail("truncated");
            std::vector<uint16_t> indices(indexCount);
            if (indexCount && !reader.Bytes(indices.data(), indexCount * sizeof(uint16_t)))
                return fail("truncated");
            for (uint32_t i = 0; i < subMeshCount; ++i) {
                uint32_t firstVertex = 0, count = 0, firstIndex = 0, subIndexCount = 0, materialSlot = 0, paletteCount = 0;
                if (!reader.Scalar(firstVertex) || !reader.Scalar(count) || !reader.Scalar(firstIndex) ||
                    !reader.Scalar(subIndexCount) || !reader.Scalar(materialSlot) || !reader.Scalar(paletteCount) ||
                    static_cast<uint64_t>(firstVertex) + count > vertexCount ||
                    static_cast<uint64_t>(firstIndex) + subIndexCount > indexCount || paletteCount > DMeshMaxPaletteBones)
                    return fail("invalid submesh in");
                MeshData::SubMesh subMesh;
                subMesh.FirstVertex = static_cast<uint32_t>(out.Vertices.size());
                subMesh.VertexCount = subIndexCount;
                subMesh.BonePalette.resize(paletteCount);
                if (paletteCount && !reader.Bytes(subMesh.BonePalette.data(), paletteCount * sizeof(uint16_t)))
                    return fail("truncated");
                for (uint16_t bone : subMesh.BonePalette)
                    if (bone >= boneCount) return fail("invalid palette in");
                out.Vertices.reserve(out.Vertices.size() + subIndexCount);
                for (uint32_t index = 0; index < subIndexCount; ++index) {
                    const uint32_t local = indices[firstIndex + index];
                    if (local >= count)
                        return fail("invalid index in");
                    out.Vertices.push_back(unique[firstVertex + local]);
                }
                out.SubMeshes.push_back(std::move(subMesh));
            }
            out.BoundingRadius = radius;
            out.ReadWriteEnabled = (flags & DMeshFlag_ReadWrite) != 0;
        } else {
            uint32_t vertexCount = 0, subMeshCount = 0, boneCount = 0;
            float radius = 0.0f;
            if (!reader.Scalar(vertexCount) || !reader.Scalar(subMeshCount) || !reader.Scalar(radius) ||
                (version >= 2 && !reader.Scalar(boneCount)) || boneCount > 65535)
                return fail("invalid");
            if (!ReadBones(reader, boneCount, out))
                return fail("truncated");
            const size_t vertexBytes = version == 1 ? 48u : (version == 2 ? 80u : 68u);
            if (vertexCount > reader.Remaining() / vertexBytes)
                return fail("truncated");
            out.Vertices.resize(vertexCount);
            for (MeshVertex& vertex : out.Vertices) {
                if (!ReadFloatVertex(reader, vertex))
                    return fail("truncated");
                if (version == 2) {
                    // Version 2 stored global u32 bone ids. Keep it loadable as a CPU-skin
                    // fallback, while newer cooked assets use local byte palette ids.
                    uint32_t legacyIndices[4]{};
                    if (!reader.Bytes(legacyIndices, sizeof(legacyIndices)) || !reader.Floats(&vertex.BoneWeights[0], 4))
                        return fail("truncated");
                    for (int influence = 0; influence < 4; ++influence)
                        vertex.BoneIndices[influence] = static_cast<uint8_t>(std::min<uint32_t>(legacyIndices[influence], 255));
                    out.LegacyBoneIndices.push_back({ legacyIndices[0], legacyIndices[1], legacyIndices[2], legacyIndices[3] });
                } else if (version == 3 && (!reader.Bytes(&vertex.BoneIndices, 4) || !reader.Floats(&vertex.BoneWeights[0], 4))) {
                    return fail("truncated");
                }
            }
            for (uint32_t i = 0; i < subMeshCount; ++i) {
                MeshData::SubMesh subMesh;
                if (!reader.Scalar(subMesh.FirstVertex) || !reader.Scalar(subMesh.VertexCount) ||
                    static_cast<uint64_t>(subMesh.FirstVertex) + subMesh.VertexCount > vertexCount)
                    return fail("truncated");
                if (version == 3) {
                    uint32_t paletteCount = 0;
                    if (!reader.Scalar(paletteCount) || paletteCount > DMeshMaxPaletteBones)
                        return fail("truncated");
                    subMesh.BonePalette.resize(paletteCount);
                    if (paletteCount && !reader.Bytes(subMesh.BonePalette.data(), paletteCount * sizeof(uint16_t)))
                        return fail("truncated");
                    for (uint16_t bone : subMesh.BonePalette)
                        if (bone >= boneCount) return fail("truncated");
                }
                out.SubMeshes.push_back(std::move(subMesh));
            }
            out.BoundingRadius = radius;
        }
        out.SourceVersion = version;
        // Tangents aren't part of the cooked format; derive them for Lit normal maps.
        GenerateTangents(out.Vertices);
        ComputeBounds(out);
        return true;
    }

}
