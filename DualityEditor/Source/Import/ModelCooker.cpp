#include "DualityEditor/Import/ModelCooker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Duality {

    namespace ModelCooker {

        namespace {
            template<typename T>
            void Append(std::vector<uint8_t>& out, const T& value) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
                out.insert(out.end(), bytes, bytes + sizeof(T));
            }
            void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
                const auto* bytes = static_cast<const uint8_t*>(data);
                out.insert(out.end(), bytes, bytes + size);
            }

            struct Influence {
                uint32_t Bone = 0;
                float Weight = 0.0f;
            };

            glm::vec3 SampleVec3(const std::vector<ImportedVec3Key>& keys, double time, const glm::vec3& fallback) {
                if (keys.empty()) return fallback;
                if (time <= keys.front().Time) return keys.front().Value;
                if (time >= keys.back().Time) return keys.back().Value;
                const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                    [](double t, const ImportedVec3Key& key) { return t < key.Time; });
                const ImportedVec3Key& a = *(next - 1);
                const double span = next->Time - a.Time;
                const float t = span > 0.0 ? static_cast<float>((time - a.Time) / span) : 0.0f;
                return glm::mix(a.Value, next->Value, t);
            }

            glm::quat SampleQuat(const std::vector<ImportedQuatKey>& keys, double time, const glm::quat& fallback) {
                if (keys.empty()) return fallback;
                if (time <= keys.front().Time) return keys.front().Value;
                if (time >= keys.back().Time) return keys.back().Value;
                const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                    [](double t, const ImportedQuatKey& key) { return t < key.Time; });
                const ImportedQuatKey& a = *(next - 1);
                const double span = next->Time - a.Time;
                const float t = span > 0.0 ? static_cast<float>((time - a.Time) / span) : 0.0f;
                return glm::normalize(glm::slerp(a.Value, next->Value, t));
            }

            float QuatAngleDegrees(const glm::quat& a, const glm::quat& b) {
                const float d = std::min(1.0f, std::fabs(glm::dot(glm::normalize(a), glm::normalize(b))));
                return glm::degrees(2.0f * std::acos(d));
            }

            float Vec3Range(const std::vector<AnimationVec3Key>& keys) {
                if (keys.empty()) return 0.0f;
                glm::vec3 lo = keys.front().Value, hi = keys.front().Value;
                for (const auto& key : keys) { lo = glm::min(lo, key.Value); hi = glm::max(hi, key.Value); }
                const glm::vec3 extent = hi - lo;
                return std::max({ extent.x, extent.y, extent.z });
            }

            float Vec3Magnitude(const std::vector<AnimationVec3Key>& keys) {
                float magnitude = 0.0f;
                for (const auto& key : keys)
                    magnitude = std::max({ magnitude, std::fabs(key.Value.x), std::fabs(key.Value.y), std::fabs(key.Value.z) });
                return magnitude;
            }

            uint16_t Quantize16(float value, float lo, float hi) {
                if (!(hi > lo)) return 0;
                const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
                return static_cast<uint16_t>(std::lround(t * 65535.0f));
            }
        }

        std::string StableGuid(const std::string& modelGuid, const std::string& key) {
            const std::string text = modelGuid + '\n' + key;
            const uint64_t high = Fnv1a64(text.data(), text.size(), 1469598103934665603ull);
            const uint64_t low = Fnv1a64(text.data(), text.size(), 0x9e3779b97f4a7c15ull ^ high);
            return HexString(high) + HexString(low);
        }

        uint32_t CookedMesh::UniqueVertexCount() const {
            uint32_t total = 0;
            for (const CookedSubMesh& subMesh : SubMeshes) total += static_cast<uint32_t>(subMesh.Vertices.size());
            return total;
        }

        uint32_t CookedMesh::IndexCount() const {
            uint32_t total = 0;
            for (const CookedSubMesh& subMesh : SubMeshes) total += static_cast<uint32_t>(subMesh.Indices.size());
            return total;
        }

        CookedMesh CookMesh(const ImportedMesh& mesh, const MeshCookOptions& options) {
            CookedMesh out;
            out.Name = mesh.Name;
            const size_t vertexCount = mesh.Positions.size();
            out.SourceVertices = static_cast<uint32_t>(vertexCount);
            const int maxInfluences = std::clamp(options.MaxBonesPerVertex, 1, static_cast<int>(DMeshMaxBonesPerVertex));
            // A triangle references at most 3 x maxInfluences bones; it must always fit a palette.
            const uint32_t maxPalette = std::clamp<uint32_t>(options.MaxPaletteBones, 3u * static_cast<uint32_t>(maxInfluences), DMeshMaxPaletteBones);
            const uint32_t maxVertices = std::clamp<uint32_t>(options.MaxVerticesPerSubMesh, 3u, DMeshMaxVerticesPerSubMesh);
            const bool skinned = options.Skinned && !mesh.Bones.empty();

            // ---- Influences: drop tiny weights, keep the strongest N, renormalize --------------
            std::vector<std::vector<Influence>> influences(vertexCount);
            if (skinned) {
                for (size_t boneIndex = 0; boneIndex < mesh.Bones.size(); ++boneIndex) {
                    const ImportedBone& bone = mesh.Bones[boneIndex];
                    MeshData::Bone cooked;
                    cooked.Name = bone.Name;
                    cooked.Offset = bone.Offset;
                    // Uniform scale conjugation (S * M * S^-1): only the translation scales.
                    cooked.Offset[3][0] *= options.Scale;
                    cooked.Offset[3][1] *= options.Scale;
                    cooked.Offset[3][2] *= options.Scale;
                    out.Bones.push_back(std::move(cooked));
                    for (const ImportedBoneWeight& weight : bone.Weights)
                        if (weight.Vertex < vertexCount && weight.Weight >= options.MinBoneWeight)
                            influences[weight.Vertex].push_back({ static_cast<uint32_t>(boneIndex), weight.Weight });
                }
                for (auto& list : influences) {
                    // Merge duplicate entries for the same bone (some exporters split them).
                    std::sort(list.begin(), list.end(), [](const Influence& a, const Influence& b) { return a.Bone < b.Bone; });
                    std::vector<Influence> merged;
                    for (const Influence& influence : list) {
                        if (!merged.empty() && merged.back().Bone == influence.Bone) merged.back().Weight += influence.Weight;
                        else merged.push_back(influence);
                    }
                    std::sort(merged.begin(), merged.end(), [](const Influence& a, const Influence& b) {
                        return a.Weight != b.Weight ? a.Weight > b.Weight : a.Bone < b.Bone;
                    });
                    if (merged.size() > static_cast<size_t>(maxInfluences)) {
                        merged.resize(static_cast<size_t>(maxInfluences));
                        ++out.ClampedInfluenceVertices;
                    }
                    float sum = 0.0f;
                    for (const Influence& influence : merged) sum += influence.Weight;
                    if (sum > 0.0f) for (Influence& influence : merged) influence.Weight /= sum;
                    list = std::move(merged);
                }
                // The skinning shaders blend exactly the stored weights: a vertex without any
                // would collapse to the origin. Bind it to a neighbour's dominant bone (Unity
                // binds such vertices to the root), falling back to bone 0.
                bool changed = true;
                for (int pass = 0; pass < 4 && changed; ++pass) {
                    changed = false;
                    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
                        const uint32_t corners[3] = { mesh.Indices[i], mesh.Indices[i + 1], mesh.Indices[i + 2] };
                        int donor = -1;
                        for (uint32_t corner : corners)
                            if (corner < vertexCount && !influences[corner].empty()) { donor = static_cast<int>(corner); break; }
                        if (donor < 0) continue;
                        for (uint32_t corner : corners) {
                            if (corner < vertexCount && influences[corner].empty()) {
                                influences[corner] = { { influences[static_cast<size_t>(donor)].front().Bone, 1.0f } };
                                ++out.UnweightedVertices;
                                changed = true;
                            }
                        }
                    }
                }
                for (auto& list : influences)
                    if (list.empty()) { list = { { 0u, 1.0f } }; ++out.UnweightedVertices; }
            }

            // Smooth normals when the source has none (Normals = None still needs a lit normal).
            std::vector<glm::vec3> generatedNormals;
            const bool hasNormals = mesh.Normals.size() == vertexCount;
            if (!hasNormals) {
                generatedNormals.assign(vertexCount, glm::vec3(0.0f));
                for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
                    const uint32_t a = mesh.Indices[i], b = mesh.Indices[i + 1], c = mesh.Indices[i + 2];
                    if (a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
                    const glm::vec3 face = glm::cross(mesh.Positions[b] - mesh.Positions[a], mesh.Positions[c] - mesh.Positions[a]);
                    generatedNormals[a] += face; generatedNormals[b] += face; generatedNormals[c] += face;
                }
            }

            auto makeVertex = [&](uint32_t index, const std::vector<uint16_t>& palette) {
                MeshVertex vertex{};
                vertex.Position = mesh.Positions[index] * options.Scale;
                vertex.TexCoord = index < mesh.UVs.size() ? mesh.UVs[index] : glm::vec2(0.0f);
                glm::vec3 normal = hasNormals ? mesh.Normals[index] : generatedNormals[index];
                const float length = glm::length(normal);
                vertex.Normal = length > 1e-6f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
                vertex.Color = (options.ImportVertexColors && index < mesh.Colors.size()) ? glm::clamp(mesh.Colors[index], 0.0f, 1.0f) : glm::vec4(1.0f);
                if (skinned) {
                    const auto& list = influences[index];
                    for (size_t influence = 0; influence < list.size() && influence < 4; ++influence) {
                        const auto found = std::find(palette.begin(), palette.end(), static_cast<uint16_t>(list[influence].Bone));
                        vertex.BoneIndices[static_cast<int>(influence)] = static_cast<uint8_t>(std::distance(palette.begin(), found));
                        vertex.BoneWeights[static_cast<int>(influence)] = list[influence].Weight;
                    }
                }
                return vertex;
            };

            // ---- Partitioning: bone palettes (<= 24) and u16 vertex ranges (<= 65535) ----------
            struct Building {
                CookedSubMesh SubMesh;
                std::unordered_map<uint32_t, uint16_t> Local;
            };
            std::vector<Building> partitions;
            std::vector<uint16_t> triangleBones;
            float radiusSq = 0.0f;
            bool hasBounds = false;
            for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3) {
                const uint32_t corners[3] = { mesh.Indices[i], mesh.Indices[i + 1], mesh.Indices[i + 2] };
                if (corners[0] >= vertexCount || corners[1] >= vertexCount || corners[2] >= vertexCount)
                    continue;
                ++out.Triangles;
                triangleBones.clear();
                if (skinned)
                    for (uint32_t corner : corners)
                        for (const Influence& influence : influences[corner])
                            if (std::find(triangleBones.begin(), triangleBones.end(), influence.Bone) == triangleBones.end())
                                triangleBones.push_back(static_cast<uint16_t>(influence.Bone));

                auto newVertices = [&](const Building& partition) {
                    uint32_t count = 0;
                    for (int c = 0; c < 3; ++c) {
                        bool seenEarlier = false;
                        for (int p = 0; p < c; ++p) seenEarlier |= corners[p] == corners[c];
                        if (!seenEarlier && partition.Local.find(corners[c]) == partition.Local.end()) ++count;
                    }
                    return count;
                };
                // Best fit: the partition needing the fewest new bones (ties -> the newest one, which
                // keeps neighbouring triangles together). Static meshes only ever extend the newest
                // partition, preserving the source triangle order.
                size_t selected = partitions.size();
                size_t bestAdditions = maxPalette + 1;
                const size_t firstCandidate = skinned ? 0 : (partitions.empty() ? 0 : partitions.size() - 1);
                for (size_t p = firstCandidate; p < partitions.size(); ++p) {
                    const auto& palette = partitions[p].SubMesh.Palette;
                    size_t additions = 0;
                    for (uint16_t bone : triangleBones)
                        if (std::find(palette.begin(), palette.end(), bone) == palette.end()) ++additions;
                    if (palette.size() + additions > maxPalette) continue;
                    if (partitions[p].SubMesh.Vertices.size() + newVertices(partitions[p]) > maxVertices) continue;
                    if (additions <= bestAdditions) {
                        selected = p;
                        bestAdditions = additions;
                    }
                }
                if (selected == partitions.size())
                    partitions.emplace_back();
                Building& partition = partitions[selected];
                for (uint16_t bone : triangleBones)
                    if (std::find(partition.SubMesh.Palette.begin(), partition.SubMesh.Palette.end(), bone) == partition.SubMesh.Palette.end())
                        partition.SubMesh.Palette.push_back(bone);
                for (uint32_t corner : corners) {
                    auto found = partition.Local.find(corner);
                    if (found == partition.Local.end()) {
                        const uint16_t local = static_cast<uint16_t>(partition.SubMesh.Vertices.size());
                        partition.SubMesh.Vertices.push_back(makeVertex(corner, partition.SubMesh.Palette));
                        const glm::vec3& p = partition.SubMesh.Vertices.back().Position;
                        radiusSq = std::max(radiusSq, glm::dot(p, p));
                        if (!hasBounds) { out.BoundsMin = out.BoundsMax = p; hasBounds = true; }
                        out.BoundsMin = glm::min(out.BoundsMin, p);
                        out.BoundsMax = glm::max(out.BoundsMax, p);
                        found = partition.Local.emplace(corner, local).first;
                    }
                    partition.SubMesh.Indices.push_back(found->second);
                }
            }
            for (Building& partition : partitions) {
                out.MaxPaletteSize = std::max(out.MaxPaletteSize, static_cast<uint32_t>(partition.SubMesh.Palette.size()));
                out.SubMeshes.push_back(std::move(partition.SubMesh));
            }
            out.Radius = std::sqrt(radiusSq);
            return out;
        }

        std::vector<uint8_t> EncodeDMesh(const CookedMesh& mesh, const DMeshWriteOptions& options) {
            std::vector<uint8_t> out;
            const uint32_t vertexCount = mesh.UniqueVertexCount(), indexCount = mesh.IndexCount();
            const uint32_t subMeshCount = static_cast<uint32_t>(mesh.SubMeshes.size());
            glm::vec3 lo(0.0f), hi(0.0f);
            glm::vec2 uvLo(0.0f), uvHi(1.0f);
            bool first = true;
            for (const CookedSubMesh& subMesh : mesh.SubMeshes)
                for (const MeshVertex& vertex : subMesh.Vertices) {
                    if (first) { lo = hi = vertex.Position; uvLo = uvHi = vertex.TexCoord; first = false; }
                    lo = glm::min(lo, vertex.Position); hi = glm::max(hi, vertex.Position);
                    uvLo = glm::min(uvLo, vertex.TexCoord); uvHi = glm::max(uvHi, vertex.TexCoord);
                }
            for (int axis = 0; axis < 2; ++axis)
                if (!(uvHi[axis] > uvLo[axis])) uvHi[axis] = uvLo[axis] + 1.0f;

            const uint32_t flags = (options.Quantize ? DMeshFlag_Quantized : 0u) | (options.ReadWrite ? DMeshFlag_ReadWrite : 0u);
            AppendBytes(out, "DMSH", 4);
            Append(out, DMeshCurrentVersion);
            Append(out, flags);
            Append(out, vertexCount);
            Append(out, indexCount);
            Append(out, subMeshCount);
            Append(out, mesh.Radius);
            const float bounds[10] = { lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, uvLo.x, uvLo.y, uvHi.x, uvHi.y };
            AppendBytes(out, bounds, sizeof(bounds));
            Append(out, static_cast<uint32_t>(mesh.Bones.size()));
            for (const MeshData::Bone& bone : mesh.Bones) {
                Append(out, static_cast<uint32_t>(bone.Name.size()));
                AppendBytes(out, bone.Name.data(), bone.Name.size());
                AppendBytes(out, &bone.Offset[0][0], sizeof(float) * 16);
            }
            for (const CookedSubMesh& subMesh : mesh.SubMeshes) {
                for (const MeshVertex& v : subMesh.Vertices) {
                    if (options.Quantize) {
                        const uint16_t position[3] = { Quantize16(v.Position.x, lo.x, hi.x), Quantize16(v.Position.y, lo.y, hi.y), Quantize16(v.Position.z, lo.z, hi.z) };
                        const uint16_t uv[2] = { Quantize16(v.TexCoord.x, uvLo.x, uvHi.x), Quantize16(v.TexCoord.y, uvLo.y, uvHi.y) };
                        int8_t normal[4] = { 0, 0, 0, 0 };
                        for (int axis = 0; axis < 3; ++axis)
                            normal[axis] = static_cast<int8_t>(std::clamp<long>(std::lround(v.Normal[axis] * 127.0f), -127, 127));
                        uint8_t color[4];
                        for (int c = 0; c < 4; ++c)
                            color[c] = static_cast<uint8_t>(std::clamp<long>(std::lround(v.Color[c] * 255.0f), 0, 255));
                        // Weights as u8 summing to exactly 255, so the shader still blends to 1.
                        uint8_t weights[4] = { 0, 0, 0, 0 };
                        int sum = 0, largest = 0;
                        for (int i = 0; i < 4; ++i) {
                            weights[i] = static_cast<uint8_t>(std::clamp<long>(std::lround(v.BoneWeights[i] * 255.0f), 0, 255));
                            sum += weights[i];
                            if (weights[i] > weights[largest]) largest = i;
                        }
                        if (sum > 0) weights[largest] = static_cast<uint8_t>(std::clamp(weights[largest] + (255 - sum), 0, 255));
                        AppendBytes(out, position, sizeof(position));
                        AppendBytes(out, uv, sizeof(uv));
                        AppendBytes(out, normal, sizeof(normal));
                        AppendBytes(out, color, sizeof(color));
                        AppendBytes(out, &v.BoneIndices, 4);
                        AppendBytes(out, weights, sizeof(weights));
                    } else {
                        const float values[12] = { v.Position.x, v.Position.y, v.Position.z, v.TexCoord.x, v.TexCoord.y,
                            v.Normal.x, v.Normal.y, v.Normal.z, v.Color.r, v.Color.g, v.Color.b, v.Color.a };
                        AppendBytes(out, values, sizeof(values));
                        AppendBytes(out, &v.BoneIndices, 4);
                        AppendBytes(out, &v.BoneWeights[0], sizeof(float) * 4);
                    }
                }
            }
            for (const CookedSubMesh& subMesh : mesh.SubMeshes)
                AppendBytes(out, subMesh.Indices.data(), subMesh.Indices.size() * sizeof(uint16_t));
            uint32_t firstVertex = 0, firstIndex = 0;
            for (const CookedSubMesh& subMesh : mesh.SubMeshes) {
                const uint32_t count = static_cast<uint32_t>(subMesh.Vertices.size());
                const uint32_t indices = static_cast<uint32_t>(subMesh.Indices.size());
                const uint32_t materialSlot = 0, paletteCount = static_cast<uint32_t>(subMesh.Palette.size());
                Append(out, firstVertex); Append(out, count); Append(out, firstIndex); Append(out, indices);
                Append(out, materialSlot); Append(out, paletteCount);
                AppendBytes(out, subMesh.Palette.data(), subMesh.Palette.size() * sizeof(uint16_t));
                firstVertex += count;
                firstIndex += indices;
            }
            return out;
        }

        bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& bytes) {
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
            const std::string temporary = path + ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output)
                    return false;
                output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!output)
                    return false;
            }
            std::filesystem::rename(temporary, path, error);
            if (error) {
                std::filesystem::remove(path, error);
                std::filesystem::rename(temporary, path, error);
            }
            if (error)
                std::filesystem::remove(temporary, error);
            return !error;
        }

        bool WriteTextAtomic(const std::string& path, const std::string& text) {
            return WriteFileAtomic(path, std::vector<uint8_t>(text.begin(), text.end()));
        }

        void ReduceKeys(std::vector<AnimationVec3Key>& keys, float tolerance) {
            if (keys.size() <= 2) {
                if (keys.size() == 2 && glm::all(glm::lessThanEqual(glm::abs(keys[0].Value - keys[1].Value), glm::vec3(tolerance))))
                    keys.resize(1);
                return;
            }
            std::vector<AnimationVec3Key> kept{ keys.front() };
            size_t anchor = 0;
            for (size_t candidate = 2; candidate < keys.size(); ++candidate) {
                bool fits = true;
                const float span = keys[candidate].Time - keys[anchor].Time;
                for (size_t i = anchor + 1; i < candidate && fits; ++i) {
                    const float t = span > 0.0f ? (keys[i].Time - keys[anchor].Time) / span : 0.0f;
                    const glm::vec3 predicted = glm::mix(keys[anchor].Value, keys[candidate].Value, t);
                    fits = glm::all(glm::lessThanEqual(glm::abs(predicted - keys[i].Value), glm::vec3(tolerance)));
                }
                if (!fits) {
                    anchor = candidate - 1;
                    kept.push_back(keys[anchor]);
                }
            }
            kept.push_back(keys.back());
            bool constant = true;
            for (const auto& key : kept)
                constant &= glm::all(glm::lessThanEqual(glm::abs(key.Value - kept.front().Value), glm::vec3(tolerance)));
            if (constant)
                kept.resize(1);
            keys = std::move(kept);
        }

        void ReduceKeys(std::vector<AnimationQuatKey>& keys, float toleranceDegrees) {
            if (keys.size() <= 2) {
                if (keys.size() == 2 && QuatAngleDegrees(keys[0].Value, keys[1].Value) <= toleranceDegrees)
                    keys.resize(1);
                return;
            }
            std::vector<AnimationQuatKey> kept{ keys.front() };
            size_t anchor = 0;
            for (size_t candidate = 2; candidate < keys.size(); ++candidate) {
                bool fits = true;
                const float span = keys[candidate].Time - keys[anchor].Time;
                for (size_t i = anchor + 1; i < candidate && fits; ++i) {
                    const float t = span > 0.0f ? (keys[i].Time - keys[anchor].Time) / span : 0.0f;
                    const glm::quat predicted = glm::slerp(keys[anchor].Value, keys[candidate].Value, t);
                    fits = QuatAngleDegrees(predicted, keys[i].Value) <= toleranceDegrees;
                }
                if (!fits) {
                    anchor = candidate - 1;
                    kept.push_back(keys[anchor]);
                }
            }
            kept.push_back(keys.back());
            bool constant = true;
            for (const auto& key : kept)
                constant &= QuatAngleDegrees(key.Value, kept.front().Value) <= toleranceDegrees;
            if (constant)
                kept.resize(1);
            keys = std::move(kept);
        }

        AnimationClipData BakeClip(const ImportedAnimation& take, const ModelClipSettings& clip, const ClipBakeOptions& options) {
            AnimationClipData out;
            const double takeEnd = std::max(0.0, take.Duration);
            double start = std::clamp(static_cast<double>(clip.Start), 0.0, takeEnd);
            double end = clip.End < 0.0f ? takeEnd : std::clamp(static_cast<double>(clip.End), 0.0, takeEnd);
            if (end < start) std::swap(start, end);
            const double duration = end - start;
            const float rate = std::max(1.0f, options.SampleRate);
            const int frames = duration > 0.0 ? std::max(1, static_cast<int>(std::ceil(duration * rate - 1e-4))) : 0;
            out.Duration = static_cast<float>(duration);
            out.SampleRate = rate;
            out.Loop = clip.Loop;

            for (const ImportedChannel& source : take.Channels) {
                AnimationChannel channel;
                channel.NodeName = source.Node;
                const bool lockRoot = options.RemoveRootMotion && !options.RootMotionNode.empty() && source.Node == options.RootMotionNode;
                glm::quat previous{ 1.0f, 0.0f, 0.0f, 0.0f };
                glm::vec3 rootStart{ 0.0f };
                for (int frame = 0; frame <= frames; ++frame) {
                    const double local = frames > 0 ? duration * frame / frames : 0.0;
                    const double time = start + local;
                    const float keyTime = static_cast<float>(local);
                    if (!source.Positions.empty()) {
                        glm::vec3 position = SampleVec3(source.Positions, time, glm::vec3(0.0f)) * options.Scale;
                        if (lockRoot) {
                            if (frame == 0) rootStart = position;
                            position.x = rootStart.x;
                            position.z = rootStart.z;
                        }
                        channel.PositionKeys.push_back({ keyTime, position });
                    }
                    if (!source.Rotations.empty()) {
                        glm::quat rotation = SampleQuat(source.Rotations, time, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                        // Keep consecutive keys in the same hemisphere so slerp takes the short way.
                        if (frame > 0 && glm::dot(previous, rotation) < 0.0f) rotation = -rotation;
                        previous = rotation;
                        channel.RotationKeys.push_back({ keyTime, rotation });
                    }
                    if (!source.Scales.empty())
                        channel.ScaleKeys.push_back({ keyTime, SampleVec3(source.Scales, time, glm::vec3(1.0f)) });
                }
                if (options.KeyframeReduction) {
                    ReduceKeys(channel.PositionKeys, std::max(1e-5f, Vec3Range(channel.PositionKeys) * options.PositionErrorPercent / 100.0f));
                    ReduceKeys(channel.RotationKeys, std::max(1e-3f, options.RotationErrorDegrees));
                    ReduceKeys(channel.ScaleKeys, std::max(1e-5f, Vec3Magnitude(channel.ScaleKeys) * options.ScaleErrorPercent / 100.0f));
                }
                out.Channels.push_back(std::move(channel));
            }
            return out;
        }

        Material MapMaterial(const ImportedMaterial& source, const std::string& baseTextureGuid, const std::string& normalTextureGuid) {
            Material material = Material::CreateDefaultLit();
            const bool blended = source.AlphaMode == ImportedAlphaMode::Blend || source.Opacity < 0.999f || source.BaseColor.a * source.Opacity < 0.999f;
            const bool masked = source.AlphaMode == ImportedAlphaMode::Mask;
            if (source.Unlit) {
                material.SetShader(blended ? BuiltinShader::UnlitTransparent : (masked ? BuiltinShader::UnlitCutout : BuiltinShader::UnlitTexture));
            } else {
                const float specular = std::max({ source.Specular.r, source.Specular.g, source.Specular.b });
                if (specular > 0.05f && source.Shininess > 1.0f) {
                    material.SetShader(BuiltinShader::LitSpecular);
                    material.SpecColor = glm::vec4(glm::clamp(source.Specular, 0.0f, 1.0f), 1.0f);
                    material.Shininess = std::clamp(source.Shininess / 128.0f, 0.01f, 1.0f);
                }
                material.Surface = blended ? SurfaceType::Transparent : (masked ? SurfaceType::Cutout : SurfaceType::Opaque);
            }
            material.Color = glm::clamp(glm::vec4(glm::vec3(source.BaseColor), source.BaseColor.a * source.Opacity), 0.0f, 1.0f);
            material.Cutoff = source.AlphaCutoff;
            material.Texture.Guid = baseTextureGuid;
            material.NormalMap.Guid = normalTextureGuid;
            material.EmissionColor = glm::vec4(glm::clamp(source.Emissive, 0.0f, 1.0f), 1.0f);
            material.Cull = source.TwoSided ? CullMode::Off : CullMode::Back;
            material.Normalize();
            return material;
        }

        uint32_t N3DSTextureBytes(int width, int height, const std::string& format, bool mipmaps) {
            int bitsPerPixel = 32;
            if (format == "RGB8") bitsPerPixel = 24;
            else if (format == "RGBA5551" || format == "RGB565" || format == "RGBA4") bitsPerPixel = 16;
            else if (format == "ETC1") bitsPerPixel = 4;
            else if (format == "ETC1A4") bitsPerPixel = 8;
            uint64_t total = 0;
            int w = std::max(1, width), h = std::max(1, height);
            for (;;) {
                // PICA200 tiles textures in 8x8 blocks: no level is smaller than one block.
                total += static_cast<uint64_t>(std::max(w, 8)) * std::max(h, 8) * bitsPerPixel / 8;
                if (!mipmaps || (w <= 8 && h <= 8)) break;
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }
            return static_cast<uint32_t>(std::min<uint64_t>(total, 0xffffffffu));
        }
    }

}
