#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Backend-agnostic 2D renderer interface. Exactly one implementation is
    // compiled in depending on the target platform:
    //   - OpenGLRenderer2D  (desktop, DE_PLATFORM_DESKTOP)
    //   - Citro2DRenderer   (device,  DE_PLATFORM_3DS)
    //
    // Every scene/scope is bracketed with an explicit Screen so dual-screen
    // output is a first-class concept from the very first renderer, not
    // something retrofitted later.
    class IRenderer2D {
    public:
        virtual ~IRenderer2D() = default;

        virtual void Init() = 0;
        virtual void Shutdown() = 0;

        // Per-renderer-instance bookkeeping bracket (currently just resets
        // GetDrawCallCount()) -- NOT the GPU frame bracket itself. On device, C3D_FrameBegin/
        // C3D_FrameEnd is owned once by the app entry point (DualityPlayer::Main.cpp), not by
        // this renderer, since a second renderer (IRenderer3D's Citro3DRenderer) may also
        // draw within the same C3D frame when a different screen uses the 3D pipeline that
        // frame -- C3D's frame bracket is process-global, singular state.
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        // Scene bracket: selects which physical screen subsequent DrawQuad calls target, and
        // clears it to clearColor -- unless `clear` is false, meaning IRenderer3D's BeginScene
        // for this same screen this frame already cleared it (RenderScreen always composites
        // both a mesh pass and a sprite pass into the same screen, exactly one of which clears
        // -- see IRenderer3D::BeginScene's own comment).
        virtual void BeginScene(Screen screen, const glm::vec4& clearColor, bool clear = true) = 0;
        virtual void EndScene() = 0;

        // position/size are in the target screen's own pixel space
        // (0,0 = top-left), position is the quad's top-left corner *before*
        // rotation. rotationDegrees rotates the quad around its own center
        // (counterclockwise-per-the-standard-rotation-matrix -- how that
        // reads visually depends on the Y-down convention both backends
        // share, but they agree with each other, which is what matters).
        // Matches TransformComponent::Rotation's degrees convention.
        // textureId (0 = none) is a backend-specific handle from
        // LoadTexture -- when non-zero the quad is drawn textured
        // (modulated by `color`) instead of flat-colored; `color` alone
        // still applies either way (a texture with color {1,1,1,1} draws
        // unmodified).
        virtual void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, float rotationDegrees = 0.0f, uint32_t textureId = 0) = 0;

        // Loads (and should internally cache) a texture from an image file
        // on disk, returning an opaque backend-specific handle for
        // DrawQuad's textureId, or 0 if the file doesn't exist or isn't a
        // decodable image. Citro2DRenderer always returns 0 -- citro2d has
        // no PNG-loading path on real hardware (needs pre-converted .t3x
        // via the tex3ds tool, a separate future asset-cooking pipeline),
        // so DrawQuad there always falls back to its flat Color.
        virtual uint32_t LoadTexture(const std::string& path) = 0;

        // Frees every texture LoadTexture has cached and clears the cache -- meant to be
        // called on a scene transition (see Duality::SceneManager), since the old scene's
        // textures are otherwise never freed for the lifetime of the process (LoadTexture's
        // cache only ever grows). Safe to call with an empty cache. Any textureId already
        // baked into an in-flight draw call becomes invalid the instant this returns -- only
        // call it between scenes, never mid-frame.
        virtual void UnloadAllTextures() = 0;

        // Number of DrawQuad calls since the last BeginFrame -- both backends are
        // unbatched (one DrawQuad = one real draw call), so this is an exact,
        // meaningful count, not an estimate. Reset to 0 by BeginFrame, incremented
        // by DrawQuad. Used by the Editor's Game panel stats overlay (FPS/draw
        // calls) -- has no purpose on real 3DS hardware, but every backend
        // implements it for interface symmetry, matching e.g. LoadTexture always
        // returning 0 on Citro2DRenderer.
        virtual uint32_t GetDrawCallCount() const = 0;
    };

}
