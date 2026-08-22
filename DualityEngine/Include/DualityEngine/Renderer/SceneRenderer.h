#pragma once

#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Shared per-screen render pass, used identically by DualityPlayer (real
    // 3DS screens) and DualityEditor's Game view (offscreen framebuffers) --
    // the only difference between platforms is which IRenderer2D backend is
    // passed in.
    //
    // Draws every (Transform, SpriteRenderer) entity in the scene, positioned
    // relative to `screen`'s primary CameraComponent entity (world position
    // and Zoom) -- cameras are their own entities, not tied to any one
    // sprite, matching Unity/Cocos convention. This is exactly what the
    // Editor's Game view and the real device show; the Editor's separate
    // Scene view (a free-roam editor-only camera, see OpenGLRenderer2D::
    // BeginCustomView) intentionally does not go through this function.
    void RenderScreen(IRenderer2D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor);

}
