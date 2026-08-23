#pragma once

#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Scene/Components.h"
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

    // Whichever AssetRef should currently be drawn for this entity's
    // sprite: its SpriteFlipbookComponent's current frame if it has one
    // assigned (non-empty Guid), else its SpriteRendererComponent::
    // Texture directly. Shared between RenderScreen (above) and the
    // Editor's Scene view, which draws sprites directly rather than
    // through RenderScreen (no CameraComponent involved there).
    AssetRef GetActiveSpriteTexture(Scene& scene, entt::entity handle);

    // Resolves `textureRef` to a loaded GPU texture handle via
    // AssetDatabase + IRenderer2D::LoadTexture, or 0 if the ref is empty or
    // doesn't resolve to an existing file (falls back to DrawQuad's flat
    // Color path either way). Shared by RenderScreen and the Editor's
    // Scene view.
    uint32_t ResolveSpriteTexture(IRenderer2D& renderer, const AssetRef& textureRef);

    // True "2 worlds" screen separation: an entity tagged (directly or via an
    // ancestor) with ScreenGroupComponent/CameraComponent for the OTHER screen is
    // excluded entirely, regardless of where it sits relative to `screen`'s camera.
    // An untagged (Ungrouped) entity is always a candidate -- its actual
    // visibility still falls out of the existing position-relative-to-camera math,
    // exactly as before this feature existed (legacy/opt-out content keeps
    // working unchanged). Shared by RenderScreen and the Editor's split Scene view.
    bool ShouldRenderOnScreen(Scene& scene, entt::entity handle, Screen screen);

}
