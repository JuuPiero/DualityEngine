#pragma once

#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Resolves a UIRectComponent's Anchor/Offset/Size into a top-left pixel position + size
    // within `rect.Screen`'s own fixed pixel dimensions -- entirely independent of any
    // CameraComponent (see UIRectComponent's own comment). The rect's own pivot always matches
    // its Anchor's alignment (a TopLeft rect's own top-left corner sits at the anchor point, a
    // BottomRight rect's own bottom-right corner does, MiddleCenter is centered on it, etc.) --
    // no separate pivot field, matching this system's "basic first pass" scope. Offset always
    // means "how far inward from that corner/edge" (or "how far from center" for the Middle/
    // Center cases), regardless of which corner -- e.g. BottomRight/Offset{10,10} sits 10px up
    // and 10px left of the screen's bottom-right corner, not 10px past it.
    void ResolveUIRect(const UIRectComponent& rect, glm::vec2& outTopLeft, glm::vec2& outSize);

    // Updates every (UIRectComponent, UIButtonComponent) entity's IsHovered/IsPressed/
    // WasClicked for this frame, from the current Input pointer state. Call exactly once per
    // frame (not once per screen, unlike RenderScreen/RenderScreenUI below), before
    // Scene::OnRuntimeUpdate so a script polling WasClicked this frame sees an accurate value
    // -- same "finalize input state before running gameplay code" ordering Input::BeginFrame's
    // own key-state updates already follow in Main.cpp/Window.cpp.
    //
    // Tests every button regardless of which Screen its UIRectComponent belongs to, since
    // Input::GetPointerPosition() is a single global value with no per-screen distinction (on
    // real 3DS hardware this is correct by construction -- touch input only ever exists for the
    // Bottom screen; on desktop it's the same rough approximation ApiShowcaseBehaviour's own
    // pointer-follow demo already accepts, see that file's own comment).
    void UpdateUIInteractions(Scene& scene);

    // The UI half of RenderScreen's composited draw (see SceneRenderer.cpp) -- draws every
    // (UIRectComponent, UIImageComponent) entity belonging to `screen`, always last/on top of
    // the mesh and sprite passes, with no camera involved at all. An entity that also has a
    // UIButtonComponent uses that component's current Normal/Hover/Pressed color instead of
    // UIImageComponent::Color (see UIButtonComponent's own comment) -- this is also how a new
    // widget type should hook in later: check for its own second component here (or add a new
    // loop) rather than changing UIRectComponent/this function's own contract.
    void RenderScreenUI(IRenderer2D& renderer, Scene& scene, Screen screen);

}
