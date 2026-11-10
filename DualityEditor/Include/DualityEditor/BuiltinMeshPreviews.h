#pragma once

#include <cstdint>

#include "DualityEngine/Renderer/MeshPrimitive.h"

namespace Duality {

    class IRenderer3D;

    // Small lit thumbnails of the built-in meshes (Cube, Sphere, ...) for the Inspector's Mesh
    // object field and picker. Rendered once per mesh, on first request, through the same
    // OpenGLRenderer3D the Scene view uses; the GL textures live until the GL context is gone.
    namespace BuiltinMeshPreviews {
        // Returns an OpenGL texture id (bottom-up rows: draw with UV (0,1)-(1,0)), 0 on failure.
        uint32_t Get(IRenderer3D& renderer, MeshPrimitive primitive);
    }

}
