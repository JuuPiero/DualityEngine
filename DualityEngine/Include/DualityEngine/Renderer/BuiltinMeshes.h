#pragma once

#include <cmath>
#include <cstring>
#include <string>

#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/MeshPrimitive.h"

namespace Duality {

    // Unity's built-in meshes (Cube, Sphere, Capsule, Cylinder, Plane, Quad) as mesh assets.
    // They have no file and no .meta: a Mesh field references one as "builtin:<Name>"
    // (e.g. MeshFilter.Mesh = "builtin:Cube"). That string is resolved here on every platform
    // -- desktop, editor and the 3DS player -- without the AssetDatabase, so the 3DS cook has
    // nothing to copy for it. Geometry: PrimitiveMeshes.h (unit size, centered on the origin).
    //
    // Header-only on purpose: the desktop GameScripts DLL does not link DualityEngine, so every
    // helper a script can call must be inline.
    namespace BuiltinMeshes {

        inline constexpr const char* Prefix = "builtin:";

        struct Info {
            MeshPrimitive Primitive;
            const char* Name; // "Cube" -- also the GameObject > 3D Object menu label
        };

        // In Unity's GameObject > 3D Object menu order.
        inline constexpr Info All[] = {
            { MeshPrimitive::Cube, "Cube" },
            { MeshPrimitive::Sphere, "Sphere" },
            { MeshPrimitive::Capsule, "Capsule" },
            { MeshPrimitive::Cylinder, "Cylinder" },
            { MeshPrimitive::Plane, "Plane" },
            { MeshPrimitive::Quad, "Quad" },
        };

        inline bool IsBuiltin(const std::string& reference) {
            return reference.compare(0, std::strlen(Prefix), Prefix) == 0;
        }
        inline bool IsBuiltin(const AssetRef& reference) { return IsBuiltin(reference.Guid); }

        // Parses a name ("Cube", Unity spelling); false if unknown.
        inline bool FromName(const std::string& name, MeshPrimitive& out) {
            for (const Info& info : All) {
                if (name == info.Name) {
                    out = info.Primitive;
                    return true;
                }
            }
            return false;
        }

        // True (and `out` set) for a known "builtin:<Name>" reference.
        inline bool TryGetPrimitive(const std::string& reference, MeshPrimitive& out) {
            return IsBuiltin(reference) && FromName(reference.substr(std::strlen(Prefix)), out);
        }
        inline bool TryGetPrimitive(const AssetRef& reference, MeshPrimitive& out) { return TryGetPrimitive(reference.Guid, out); }

        inline const char* NameOf(MeshPrimitive primitive) {
            for (const Info& info : All)
                if (info.Primitive == primitive)
                    return info.Name;
            return "Cube";
        }

        // "builtin:Cube" for MeshPrimitive::Cube.
        inline std::string ReferenceFor(MeshPrimitive primitive) { return std::string(Prefix) + NameOf(primitive); }
        inline AssetRef Ref(MeshPrimitive primitive) { return AssetRef{ ReferenceFor(primitive) }; }

        // Bounding-sphere radius of the unit-sized mesh before Transform scale.
        inline float BoundingRadius(MeshPrimitive primitive) {
            switch (primitive) {
                case MeshPrimitive::Sphere:
                case MeshPrimitive::Capsule:  return 0.5f;                                  // height 1, radius 0.25
                case MeshPrimitive::Cylinder: return std::sqrt(0.25f * 0.25f + 0.5f * 0.5f); // height 1, radius 0.25
                case MeshPrimitive::Plane:
                case MeshPrimitive::Quad:     return 0.5f * std::sqrt(2.0f);
                case MeshPrimitive::Cube:
                default:                      return 0.5f * std::sqrt(3.0f);
            }
        }
    }

}
