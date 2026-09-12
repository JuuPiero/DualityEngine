#pragma once

#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Finds the Canvas ancestor that owns an element, or an empty entity when the hierarchy is
    // invalid. New UI must be parented below a Canvas; EnsureUIElementsHaveCanvas upgrades old
    // scenes in memory so they remain usable and save back in the Canvas-first layout.
    Entity FindOwningCanvas(Scene& scene, Entity entity);
    bool IsUIElementOnCanvas(Scene& scene, Entity entity, Screen screen);
    void EnsureUIElementsHaveCanvas(Scene& scene);

    // Resolves a UIRectComponent's Anchor/Offset/Size into a top-left pixel position + size.
    // If `entity`'s HierarchyComponent::Parent also has a UIRectComponent, the rect is resolved
    // relative to the PARENT's own resolved rect (recursively) instead of the raw screen -- this
    // is what makes nested Canvas UI panels resolve visually rather than every element
    // positioning independently against the screen. A root UI element resolves against its
    // owning Canvas's fixed physical screen dimensions.
    // pixel dimensions, same as before nesting existed. The rect's own pivot always matches its
    // Anchor's alignment (a TopLeft rect's own top-left corner sits at the anchor point, a
    // BottomRight rect's own bottom-right corner does, MiddleCenter is centered on it, etc.) --
    // no separate pivot field, matching this system's "basic first pass" scope. Offset always
    // means "how far inward from that corner/edge" (or "how far from center" for the Middle/
    // Center cases), regardless of which corner -- e.g. BottomRight/Offset{10,10} sits 10px up
    // and 10px left of its parent's (or the screen's) bottom-right corner, not 10px past it.
    void ResolveUIRect(Scene& scene, Entity entity, glm::vec2& outTopLeft, glm::vec2& outSize);

    // Updates every (UIRectComponent, UIButtonComponent) entity's IsHovered/IsPressed/
    // WasClicked for this frame, from the current Input pointer state. Call exactly once per
    // frame (not once per screen, unlike RenderScreen/RenderScreenUI below), before
    // Scene::OnRuntimeUpdate so a script polling WasClicked this frame sees an accurate value
    // -- same "finalize input state before running gameplay code" ordering InputManager::BeginFrame's
    // own key-state updates already follow in Main.cpp/Window.cpp.
    //
    // Only tests buttons on the screen InputManager::GetPointerScreen() reports -- Top and Bottom
    // share overlapping local pixel ranges, so a button is only actually reachable by a pointer
    // that's really over its own screen (see InputManager::GetPointerScreen's own comment).
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
