#pragma once

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
        // (0,0 = top-left), position is the quad's top-left corner.
        virtual void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) = 0;
    };

}
