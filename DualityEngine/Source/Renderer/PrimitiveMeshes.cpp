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
    }

    const std::vector<MeshVertex>& GetPrimitiveMesh(MeshPrimitive primitive) {
        static const std::vector<MeshVertex> cube = MakeCube();
        static const std::vector<MeshVertex> sphere = MakeSphere();
        static const std::vector<MeshVertex> plane = MakePlane();

        switch (primitive) {
            case MeshPrimitive::Sphere: return sphere;
            case MeshPrimitive::Plane: return plane;
            default: return cube;
        }
    }

}
