#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/MeshPrimitive.h"
#include "DualityEngine/Renderer/ProjectionType.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Backend-agnostic 3D renderer interface -- deliberately separate from IRenderer2D, not a
    // superset of it: a pixel-space quad and a perspective-projected mesh don't share enough
    // shape to unify into one contract. Exactly one implementation is compiled in per
    // platform, same convention as IRenderer2D:
    //   - OpenGLRenderer3D  (desktop, DE_PLATFORM_DESKTOP)
    //   - Citro3DRenderer   (device,  DE_PLATFORM_3DS)
    //
    // A screen always draws BOTH this interface's mesh content AND IRenderer2D's sprite content
    // into the same target every frame (see SceneRenderer.cpp's RenderScreen) -- matching
    // Unity's own convention that a camera's Projection is just a lens property, not a switch
    // between mutually-exclusive renderers. Unlit only: no lighting/material system yet (see
    // ROADMAP.md).
    class IRenderer3D {
    public:
        virtual ~IRenderer3D() = default;

        virtual void Init() = 0;
        virtual void Shutdown() = 0;

        // Scene bracket: camera parameters, deliberately NOT a pre-composed view/projection
        // matrix -- each backend builds its own matrices using its native math library (GLM
        // on desktop, citro3d's Mtx_PerspTilt/Mtx_OrthoTilt + Mtx_Inverse of the camera's own
        // composed world transform on 3DS). GLM is column-major;
        // citro3d's C3D_Mtx is row-major with reversed-component FVecs ({w,z,y,x} in memory,
        // confirmed against the real installed header) -- converting a pre-composed matrix
        // between those two layouts byte-for-byte is exactly the kind of subtle, hard-to-
        // verify bug this interface shape avoids entirely. cameraPosition/
        // cameraRotationDegrees match TransformComponent's own shape exactly (a camera's view
        // matrix is just the inverse of its own world transform) -- the caller
        // (SceneRenderer.cpp) reads these straight from Scene::GetWorldTransform, no matrix
        // math on the caller side either.
        //
        // `projection` picks Perspective (uses fovDegrees) or Orthographic (uses
        // orthoHalfHeight, in world units -- the caller computes this from the camera's own
        // Zoom the exact same way IRenderer2D::DrawQuad's own pixel-space math does,
        // `screenHeightPixels * 0.5f / zoom`, so a mesh and a sprite at the same world X/Y
        // land on the same screen pixel when Zoom == 1). `clear` is false when IRenderer2D's
        // BeginScene for this same screen this frame already cleared it (RenderScreen always
        // clears exactly once per screen, whichever pass runs first) -- true when nothing else
        // will clear (e.g. no camera at all for this screen).
        virtual void BeginScene(Screen screen, ProjectionType projection, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float orthoHalfHeight, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor, bool clear = true) = 0;
        virtual void EndScene() = 0;

        // translation/rotationDegrees/scale, not a pre-composed model matrix -- same
        // reasoning as BeginScene above, and matches Scene::GetWorldTransform's own
        // TransformComponent{Translation,Rotation,Scale} shape exactly. textureId (0 = none)
        // is a backend-specific handle from LoadTexture, same convention as
        // IRenderer2D::DrawQuad; `color` modulates a resolved texture or applies as a flat
        // color when textureId is 0. meshHandle (0 = none) is a backend-specific handle from
        // LoadMesh -- when non-zero, the imported mesh it refers to is drawn INSTEAD of
        // `primitive` (which is then ignored), matching MeshRendererComponent::Mesh's own
        // "empty AssetRef falls back to the procedural Primitive" convention.
        virtual void DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId = 0) = 0;

        // Same guid->path->LoadTexture chain as IRenderer2D::LoadTexture (see
        // SceneRenderer.cpp's ResolveSpriteTexture) -- a mesh's Texture AssetRef resolves the
        // exact same way a sprite's does, just handed to this interface instead.
        virtual uint32_t LoadTexture(const std::string& path) = 0;

        // Uploads an imported mesh (see Asset/MeshLoader.h) to a backend-specific GPU-resident
        // buffer, returning an opaque handle for DrawMesh's meshHandle parameter (0 = load
        // failed or the file didn't resolve to any vertices). Cached by path internally, same
        // convention as LoadTexture.
        virtual uint32_t LoadMesh(const std::string& path) = 0;

        // Same "free everything cached, meant for scene transitions" contract as
        // IRenderer2D::UnloadAllTextures -- see that method's own comment. Separate from
        // UnloadAllMeshes below since textures and meshes are independent caches here.
        virtual void UnloadAllTextures() = 0;

        // Frees every mesh LoadMesh has uploaded and clears the cache -- the 3 built-in
        // procedural primitives (Cube/Sphere/Plane, MeshPrimitive) are NOT affected, only
        // imported ones. Same scene-transition-only calling contract as UnloadAllTextures.
        virtual void UnloadAllMeshes() = 0;

        // Unlike IRenderer2D (reset by BeginFrame, a whole-frame bracket), this interface has
        // no BeginFrame -- there's no cross-screen GPU frame concept exposed at this level
        // (owned by the app entry point instead, see Citro2DRenderer::Init's comment), so
        // implementations reset this once per BeginScene/EndScene bracket instead.
        virtual uint32_t GetDrawCallCount() const = 0;
    };

}
