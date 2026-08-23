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

        // Frame bracket: on device, C3D_FrameBegin/C3D_FrameEnd.
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        // Scene bracket: selects which physical screen subsequent DrawQuad
        // calls target, and clears it to clearColor.
        virtual void BeginScene(Screen screen, const glm::vec4& clearColor) = 0;
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
    };

}
