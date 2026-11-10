#pragma once

// The UI widgets / effects the UI renderer (Renderer/UIRenderer.h) delegates here: Scrollbar
// handle, Dropdown caption + open list, Shadow / Outline.

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    class IRenderer2D;
    class Scene;


    // The UI renderer of the frame, for Content Size Fitter text measurement.
    void SetUILayoutRenderer(IRenderer2D* renderer);

    // Scrollbar: track in `trackColor` (the entity's UI Image color) and the handle on top.
    void DrawUIScrollbar(IRenderer2D& renderer, Scene& scene, entt::entity handle, const glm::vec2& topLeft,
                         const glm::vec2& size, const glm::vec4& trackColor, float alpha);
    // Dropdown caption: box, selected option text and an arrow.
    void DrawUIDropdownCaption(IRenderer2D& renderer, Scene& scene, entt::entity handle, const glm::vec2& topLeft,
                               const glm::vec2& size, const glm::vec4& boxColor, float alpha);
    // Every open Dropdown list of `screen`, drawn last (on top of the whole canvas, unclipped).
    void DrawUIDropdownLists(IRenderer2D& renderer, Scene& scene, Screen screen);

    // Draws the UI Shadow / UI Outline copies of a graphic before the graphic itself.
    // `drawAt(offset, color)` draws the graphic shifted by `offset` pixels in `color`.
    template<typename DrawFn>
    void DrawUIGraphicEffects(Scene& scene, entt::entity handle, const glm::vec4& graphicColor, DrawFn&& drawAt) {
        auto& registry = scene.Registry();
        if (const auto* shadow = registry.try_get<UIShadowComponent>(handle); shadow && shadow->Enabled) {
            glm::vec4 color = shadow->EffectColor;
            if (shadow->UseGraphicAlpha)
                color.a *= graphicColor.a;
            drawAt(shadow->EffectDistance, color);
        }
        if (const auto* outline = registry.try_get<UIOutlineComponent>(handle); outline && outline->Enabled) {
            glm::vec4 color = outline->EffectColor;
            if (outline->UseGraphicAlpha)
                color.a *= graphicColor.a;
            const glm::vec2 d = outline->EffectDistance;
            drawAt(glm::vec2(d.x, d.y), color);
            drawAt(glm::vec2(-d.x, d.y), color);
            drawAt(glm::vec2(d.x, -d.y), color);
            drawAt(glm::vec2(-d.x, -d.y), color);
        }
    }

}
