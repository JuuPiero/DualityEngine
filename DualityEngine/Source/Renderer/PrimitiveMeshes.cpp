#include "DualityEngine/Renderer/PrimitiveMeshes.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Duality {

    void GenerateTangents(std::vector<MeshVertex>& vertices) {
        // Smooth tangents: faces sharing a (position, normal, UV-handedness) corner average
        // their UV-space tangent, exactly what a normal map baked from a smooth mesh expects.
        struct Key {
            float Px, Py, Pz, Nx, Ny, Nz; int Sign;
            bool operator==(const Key& o) const { return std::memcmp(this, &o, sizeof(Key)) == 0; }
        };
        struct KeyHash {
            size_t operator()(const Key& key) const {
                const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
                size_t hash = 1469598103934665603ull;
                for (size_t i = 0; i < sizeof(Key); ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
                return hash;
            }
        };
        auto makeKey = [](const MeshVertex& v, int sign) {
            Key key;
            std::memset(&key, 0, sizeof(key));
            key.Px = v.Position.x; key.Py = v.Position.y; key.Pz = v.Position.z;
            key.Nx = v.Normal.x; key.Ny = v.Normal.y; key.Nz = v.Normal.z; key.Sign = sign;
            return key;
        };

        std::vector<glm::vec3> faceTangents(vertices.size() / 3);
        std::vector<int> faceSigns(vertices.size() / 3, 1);
        std::unordered_map<Key, glm::vec3, KeyHash> accumulated;
        accumulated.reserve(vertices.size());
        for (size_t face = 0; face * 3 + 2 < vertices.size(); ++face) {
            const MeshVertex& a = vertices[face * 3];
            const MeshVertex& b = vertices[face * 3 + 1];
            const MeshVertex& c = vertices[face * 3 + 2];
            const glm::vec3 e1 = b.Position - a.Position, e2 = c.Position - a.Position;
            const glm::vec2 d1 = b.TexCoord - a.TexCoord, d2 = c.TexCoord - a.TexCoord;
            const float det = d1.x * d2.y - d2.x * d1.y;
            glm::vec3 tangent = std::abs(det) > 1e-12f ? (e1 * d2.y - e2 * d1.y) / det : e1; // area-weighted
            faceTangents[face] = tangent;
            faceSigns[face] = det < 0.0f ? -1 : 1;
            for (int corner = 0; corner < 3; ++corner)
                accumulated[makeKey(vertices[face * 3 + corner], faceSigns[face])] += tangent;
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            MeshVertex& vertex = vertices[i];
            const size_t face = i / 3;
            glm::vec3 tangent = face < faceTangents.size() ? accumulated[makeKey(vertex, faceSigns[face])] : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 normal = vertex.Normal;
            const float normalLength = glm::length(normal);
            normal = normalLength > 1e-6f ? normal / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
            tangent -= normal * glm::dot(normal, tangent); // Gram-Schmidt against the vertex normal
            if (glm::dot(tangent, tangent) < 1e-12f) {
                // Degenerate UVs (or none at all): any vector perpendicular to the normal works,
                // since without a normal map lighting never looks at the tangent's rotation.
                tangent = std::abs(normal.x) < 0.9f ? glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f)) : glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));
                tangent = glm::cross(tangent, normal);
            }
            tangent = glm::normalize(tangent);
            vertex.Tangent = glm::i8vec4(
                static_cast<int8_t>(std::lround(tangent.x * 127.0f)),
                static_cast<int8_t>(std::lround(tangent.y * 127.0f)),
                static_cast<int8_t>(std::lround(tangent.z * 127.0f)),
                static_cast<int8_t>(face < faceSigns.size() && faceSigns[face] < 0 ? -127 : 127));
        }
    }

    namespace {
        void GenerateFlatNormals(std::vector<MeshVertex>& vertices) {
            for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
                MeshVertex& a = vertices[i];
                MeshVertex& b = vertices[i + 1];
                MeshVertex& c = vertices[i + 2];
                glm::vec3 normal = glm::cross(b.Position - a.Position, c.Position - a.Position);
                float lengthSq = glm::dot(normal, normal);
                normal = lengthSq > 0.000001f ? normal / std::sqrt(lengthSq) : glm::vec3(0.0f, 1.0f, 0.0f);
                a.Normal = b.Normal = c.Normal = normal;
            }
        }

        // Six faces x 2 triangles x 3 verts, unit cube (-0.5..+0.5) -- same vertex layout as
        // References/devkitpro-3ds-templates/graphics/gpu/textured_cube's own vertex_list,
        // minus the per-vertex normal (unlit, see IRenderer3D.h).
        std::vector<MeshVertex> MakeCube() {
            return {
                // +Z
                { {-0.5f,-0.5f,+0.5f}, {0.0f,0.0f} }, { {+0.5f,-0.5f,+0.5f}, {1.0f,0.0f} }, { {+0.5f,+0.5f,+0.5f}, {1.0f,1.0f} },
                { {+0.5f,+0.5f,+0.5f}, {1.0f,1.0f} }, { {-0.5f,+0.5f,+0.5f}, {0.0f,1.0f} }, { {-0.5f,-0.5f,+0.5f}, {0.0f,0.0f} },
                // -Z
                { {-0.5f,-0.5f,-0.5f}, {0.0f,0.0f} }, { {-0.5f,+0.5f,-0.5f}, {1.0f,0.0f} }, { {+0.5f,+0.5f,-0.5f}, {1.0f,1.0f} },
                { {+0.5f,+0.5f,-0.5f}, {1.0f,1.0f} }, { {+0.5f,-0.5f,-0.5f}, {0.0f,1.0f} }, { {-0.5f,-0.5f,-0.5f}, {0.0f,0.0f} },
                // +X
                { {+0.5f,-0.5f,-0.5f}, {0.0f,0.0f} }, { {+0.5f,+0.5f,-0.5f}, {1.0f,0.0f} }, { {+0.5f,+0.5f,+0.5f}, {1.0f,1.0f} },
                { {+0.5f,+0.5f,+0.5f}, {1.0f,1.0f} }, { {+0.5f,-0.5f,+0.5f}, {0.0f,1.0f} }, { {+0.5f,-0.5f,-0.5f}, {0.0f,0.0f} },
                // -X
                { {-0.5f,-0.5f,-0.5f}, {0.0f,0.0f} }, { {-0.5f,-0.5f,+0.5f}, {1.0f,0.0f} }, { {-0.5f,+0.5f,+0.5f}, {1.0f,1.0f} },
                { {-0.5f,+0.5f,+0.5f}, {1.0f,1.0f} }, { {-0.5f,+0.5f,-0.5f}, {0.0f,1.0f} }, { {-0.5f,-0.5f,-0.5f}, {0.0f,0.0f} },
                // +Y
                { {-0.5f,+0.5f,-0.5f}, {0.0f,0.0f} }, { {-0.5f,+0.5f,+0.5f}, {1.0f,0.0f} }, { {+0.5f,+0.5f,+0.5f}, {1.0f,1.0f} },
                { {+0.5f,+0.5f,+0.5f}, {1.0f,1.0f} }, { {+0.5f,+0.5f,-0.5f}, {0.0f,1.0f} }, { {-0.5f,+0.5f,-0.5f}, {0.0f,0.0f} },
                // -Y
                { {-0.5f,-0.5f,-0.5f}, {0.0f,0.0f} }, { {+0.5f,-0.5f,-0.5f}, {1.0f,0.0f} }, { {+0.5f,-0.5f,+0.5f}, {1.0f,1.0f} },
                { {+0.5f,-0.5f,+0.5f}, {1.0f,1.0f} }, { {-0.5f,-0.5f,+0.5f}, {0.0f,1.0f} }, { {-0.5f,-0.5f,-0.5f}, {0.0f,0.0f} },
            };
        }

        // A single quad lying flat in the XZ plane (Y=0), matching Unity's own Plane
        // primitive convention (a horizontal ground plane, not a vertical XY billboard).
        // Counter-clockwise seen from +Y, so the generated flat normal points UP and back-face
        // culling keeps the visible side (it used to be wound the other way: its normal faced
        // down, which made a VertexLit ground plane go dark under a sun shining from above).
        std::vector<MeshVertex> MakePlane() {
            return {
                { {-0.5f, 0.0f, -0.5f}, {0.0f, 0.0f} }, { {+0.5f, 0.0f, +0.5f}, {1.0f, 1.0f} }, { {+0.5f, 0.0f, -0.5f}, {1.0f, 0.0f} },
                { {+0.5f, 0.0f, +0.5f}, {1.0f, 1.0f} }, { {-0.5f, 0.0f, -0.5f}, {0.0f, 0.0f} }, { {-0.5f, 0.0f, +0.5f}, {0.0f, 1.0f} },
            };
        }

        // A low-poly UV sphere, radius 0.5 (matching the cube's unit-size convention),
        // generated as a flat non-indexed triangle list -- each latitude/longitude quad
        // becomes 2 triangles, same "duplicate verts at seams, no index buffer" convention as
        // the cube/plane above (and every citro3d reference example).
        std::vector<MeshVertex> MakeSphere() {
            constexpr int Rings = 8, Segments = 16;
            constexpr float Radius = 0.5f;
            constexpr float Pi = 3.14159265358979323846f;

            auto vertexAt = [&](int ring, int segment) {
                float v = static_cast<float>(ring) / Rings;
                float u = static_cast<float>(segment) / Segments;
                float phi = v * Pi;          // 0 (top pole) .. Pi (bottom pole)
                float theta = u * 2.0f * Pi; // 0 .. 2*Pi around
                glm::vec3 position{
                    Radius * std::sin(phi) * std::cos(theta),
                    Radius * std::cos(phi),
                    Radius * std::sin(phi) * std::sin(theta)
                };
                return MeshVertex{ position, { u, v } };
            };

            std::vector<MeshVertex> vertices;
            vertices.reserve(Rings * Segments * 6);
            for (int ring = 0; ring < Rings; ring++) {
                for (int segment = 0; segment < Segments; segment++) {
                    MeshVertex topLeft = vertexAt(ring, segment);
                    MeshVertex topRight = vertexAt(ring, segment + 1);
                    MeshVertex bottomLeft = vertexAt(ring + 1, segment);
                    MeshVertex bottomRight = vertexAt(ring + 1, segment + 1);

                    // Counter-clockwise seen from outside (front faces survive Cull Back).
                    vertices.push_back(topLeft);
                    vertices.push_back(bottomRight);
                    vertices.push_back(bottomLeft);

                    vertices.push_back(bottomRight);
                    vertices.push_back(topLeft);
                    vertices.push_back(topRight);
                }
            }
            return vertices;
        }

        // Y-up capsule fitting the unit box like the cube/sphere above: total height 1,
        // radius 0.25 -- the same proportions as Unity's capsule (height 2, radius 0.5), half
        // size. A cylinder body (y in -0.25..+0.25) between two hemispheres whose rings are
        // spaced by angle, with real capsule normals (radial on the body, from each
        // hemisphere's center on the caps) -- not sphere normals. Scale it uniformly to keep
        // round caps (a non-uniform scale squashes them, as in Unity).
        // CapsuleCollider3DComponent's Radius/Height are independent of this mesh.
        std::vector<MeshVertex> MakeCapsule() {
            constexpr float Radius = 0.25f;
            constexpr float Height = 1.0f;
            constexpr float HalfBody = Height * 0.5f - Radius;
            constexpr int Segments = 16;
            constexpr int CapRings = 6; // rings per hemisphere
            constexpr float Pi = 3.14159265358979323846f;

            // Profile from the bottom pole to the top pole: (y, ring radius, normal in the
            // (radial, y) plane). The body is the single span between the two equators.
            struct ProfilePoint { float Y, R, NormalR, NormalY; };
            std::vector<ProfilePoint> profile;
            profile.reserve(CapRings * 2 + 2);
            for (int i = 0; i <= CapRings; ++i) { // bottom cap: -90deg .. 0deg
                const float angle = -0.5f * Pi + 0.5f * Pi * static_cast<float>(i) / CapRings;
                profile.push_back({ -HalfBody + Radius * std::sin(angle), Radius * std::cos(angle), std::cos(angle), std::sin(angle) });
            }
            for (int i = 0; i <= CapRings; ++i) { // top cap: 0deg .. 90deg
                const float angle = 0.5f * Pi * static_cast<float>(i) / CapRings;
                profile.push_back({ HalfBody + Radius * std::sin(angle), Radius * std::cos(angle), std::cos(angle), std::sin(angle) });
            }
            profile.front().R = profile.back().R = 0.0f; // exact poles

            // V follows arc length so a texture isn't stretched over the body.
            std::vector<float> arc(profile.size(), 0.0f);
            for (size_t i = 1; i < profile.size(); ++i)
                arc[i] = arc[i - 1] + std::hypot(profile[i].Y - profile[i - 1].Y, profile[i].R - profile[i - 1].R);

            auto vertexAt = [&](size_t ring, int segment) {
                const ProfilePoint& p = profile[ring];
                const float u = static_cast<float>(segment) / Segments;
                const float angle = u * 2.0f * Pi;
                const float c = std::cos(angle), s = std::sin(angle);
                MeshVertex vertex{ { p.R * c, p.Y, p.R * s }, { u, arc[ring] / arc.back() } };
                vertex.Normal = glm::vec3(p.NormalR * c, p.NormalY, p.NormalR * s);
                return vertex;
            };

            std::vector<MeshVertex> vertices;
            vertices.reserve((profile.size() - 1) * Segments * 6);
            for (size_t ring = 0; ring + 1 < profile.size(); ++ring) {
                if (profile[ring + 1].Y - profile[ring].Y < 1e-6f)
                    continue; // the duplicated equator point where the body would be zero-length
                for (int segment = 0; segment < Segments; ++segment) {
                    const MeshVertex p00 = vertexAt(ring, segment);
                    const MeshVertex p10 = vertexAt(ring, segment + 1);
                    const MeshVertex p01 = vertexAt(ring + 1, segment);
                    const MeshVertex p11 = vertexAt(ring + 1, segment + 1);

                    // Counter-clockwise seen from outside (front faces survive Cull Back).
                    vertices.push_back(p00);
                    vertices.push_back(p01);
                    vertices.push_back(p11);
                    vertices.push_back(p11);
                    vertices.push_back(p10);
                    vertices.push_back(p00);
                }
            }
            return vertices;
        }
    }

    const std::vector<MeshVertex>& GetPrimitiveMesh(MeshPrimitive primitive) {
        static const std::vector<MeshVertex> cube = [] { auto v = MakeCube(); GenerateFlatNormals(v); GenerateTangents(v); return v; }();
        static const std::vector<MeshVertex> sphere = [] { auto v = MakeSphere(); for (auto& vertex : v) vertex.Normal = glm::normalize(vertex.Position); GenerateTangents(v); return v; }();
        static const std::vector<MeshVertex> plane = [] { auto v = MakePlane(); GenerateFlatNormals(v); GenerateTangents(v); return v; }();
        static const std::vector<MeshVertex> capsule = [] { auto v = MakeCapsule(); GenerateTangents(v); return v; }(); // normals set by MakeCapsule

        switch (primitive) {
            case MeshPrimitive::Sphere: return sphere;
            case MeshPrimitive::Plane: return plane;
            case MeshPrimitive::Capsule: return capsule;
            default: return cube;
        }
    }

}
