#include "DualityEngine/Renderer/PrimitiveMeshes.h"

#include <cmath>

namespace Duality {

    namespace {
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
        std::vector<MeshVertex> MakePlane() {
            return {
                { {-0.5f, 0.0f, -0.5f}, {0.0f, 0.0f} }, { {+0.5f, 0.0f, -0.5f}, {1.0f, 0.0f} }, { {+0.5f, 0.0f, +0.5f}, {1.0f, 1.0f} },
                { {+0.5f, 0.0f, +0.5f}, {1.0f, 1.0f} }, { {-0.5f, 0.0f, +0.5f}, {0.0f, 1.0f} }, { {-0.5f, 0.0f, -0.5f}, {0.0f, 0.0f} },
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

                    vertices.push_back(topLeft);
                    vertices.push_back(bottomLeft);
                    vertices.push_back(bottomRight);

                    vertices.push_back(bottomRight);
                    vertices.push_back(topRight);
                    vertices.push_back(topLeft);
                }
            }
            return vertices;
        }

        // Y-up capsule, total height 1 and radius 0.5 (same unit-size convention as the cube/
        // sphere above) -- matches CapsuleCollider3DComponent's Height/Radius semantics when
        // drawn via MeshRendererComponent::Scale.
        std::vector<MeshVertex> MakeCapsule() {
            constexpr float Radius = 0.5f;
            constexpr float Height = 1.0f;
            constexpr int Segments = 16;
            constexpr int Rings = 12;
            constexpr float Pi = 3.14159265358979323846f;

            float halfBody = std::max(0.0f, (Height - 2.0f * Radius) * 0.5f);
            float yBottom = -halfBody - Radius;
            float yTop = halfBody + Radius;

            auto radiusAtY = [halfBody](float y) {
                if (y > halfBody) {
                    float d = y - halfBody;
                    return std::sqrt(std::max(Radius * Radius - d * d, 0.0f));
                }
                if (y < -halfBody) {
                    float d = -halfBody - y;
                    return std::sqrt(std::max(Radius * Radius - d * d, 0.0f));
                }
                return Radius;
            };

            std::vector<MeshVertex> vertices;
            vertices.reserve(Rings * Segments * 6);
            for (int ring = 0; ring < Rings; ring++) {
                float v0 = static_cast<float>(ring) / Rings;
                float v1 = static_cast<float>(ring + 1) / Rings;
                float y0 = yBottom + v0 * (yTop - yBottom);
                float y1 = yBottom + v1 * (yTop - yBottom);
                float r0 = radiusAtY(y0);
                float r1 = radiusAtY(y1);

                for (int segment = 0; segment < Segments; segment++) {
                    float u0 = static_cast<float>(segment) / Segments;
                    float u1 = static_cast<float>(segment + 1) / Segments;
                    float a0 = u0 * 2.0f * Pi;
                    float a1 = u1 * 2.0f * Pi;

                    MeshVertex p00{ { r0 * std::cos(a0), y0, r0 * std::sin(a0) }, { u0, v0 } };
                    MeshVertex p10{ { r0 * std::cos(a1), y0, r0 * std::sin(a1) }, { u1, v0 } };
                    MeshVertex p01{ { r1 * std::cos(a0), y1, r1 * std::sin(a0) }, { u0, v1 } };
                    MeshVertex p11{ { r1 * std::cos(a1), y1, r1 * std::sin(a1) }, { u1, v1 } };

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
        static const std::vector<MeshVertex> cube = MakeCube();
        static const std::vector<MeshVertex> sphere = MakeSphere();
        static const std::vector<MeshVertex> plane = MakePlane();
        static const std::vector<MeshVertex> capsule = MakeCapsule();

        switch (primitive) {
            case MeshPrimitive::Sphere: return sphere;
            case MeshPrimitive::Plane: return plane;
            case MeshPrimitive::Capsule: return capsule;
            default: return cube;
        }
    }

}
