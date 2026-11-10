#pragma once

namespace Duality {

    // The procedural meshes every renderer backend uploads once at Init() (see
    // PrimitiveMeshes.h). Scenes never store this enum: a MeshFilter references a built-in
    // mesh by its asset reference "builtin:<Name>" (see BuiltinMeshes.h), exactly like Unity's
    // built-in Cube/Sphere/... meshes are ordinary Mesh assets. Kept in its own header because
    // renderer-facing headers (RenderView.h, IRenderer3D.h) need it without Components.h.
    // New values go at the end (the backends index their GPU buffers by this value).
    enum class MeshPrimitive {
        Cube,
        Sphere,
        Plane,
        Capsule,
        Cylinder,
        Quad,
        Count // sentinel -- not a valid primitive
    };

}
