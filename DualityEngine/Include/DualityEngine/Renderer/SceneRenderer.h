#pragma once

#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Shared per-screen render pass, used identically by DualityPlayer (real
    // 3DS screens) and DualityEditor (offscreen framebuffers) -- the only
    // difference between platforms is which IRenderer2D backend is passed
    // in.
    //
    // Current rule (interim, matches how scenes are authored today: one
    // entity = one camera + its own sprite): draws `screen`'s primary
    // camera's own sprite, if that same entity has one. Proper generic
    // multi-sprite-per-screen composition (visibility via camera
    // position/viewport rather than "same entity") is a later renderer
    // phase.
    void RenderScreen(IRenderer2D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor);

}
