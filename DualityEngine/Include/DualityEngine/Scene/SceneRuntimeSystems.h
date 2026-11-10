#pragma once

// Per-frame systems that are not physics (physics: Source/Scene/Systems/Physics*System.cpp),
// called by Scene::OnRuntimeUpdate and the UI renderer. Every system is a view over its
// components -- free when the component is unused.

#include <entt.hpp>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Scene/Components.h"

namespace Duality {

    class Scene;

    struct TilemapData {
        int Width = 0;
        int Height = 0;
        std::vector<int> Tiles;
    };

    class TilemapLoader {
    public:
        static TilemapData Load(const std::string& path);
    };

    void UpdateSceneRuntimeSystems(Scene& scene, float deltaTime);
    void ClearSceneRuntimeSystems(Scene& scene);

    std::vector<Particle>& GetParticlePool(entt::entity handle);

    // ---- Systems/ConstraintSystem.cpp ----------------------------------------------------------
    // After every script's LateUpdate (Scene::OnRuntimeUpdate): constraints, Billboard, Trail
    // Renderer point sampling / Autodestruct.
    void UpdateLateRuntimeSystems(Scene& scene, float deltaTime);

    // ---- Systems/UISystem.cpp ------------------------------------------------------------------
    // With the other UI layout (UpdateSceneRuntimeSystems): Grid Layout Group, Content Size
    // Fitter, Aspect Ratio Fitter, and Scroll Rect inertia / elasticity.
    void UpdateUILayoutSystems(Scene& scene, float deltaTime);

    // End of UpdateUIInteractions: pointer handling of Scroll Rect, Scrollbar and Dropdown.
    void UpdateUIWidgetInteractions(Scene& scene);

    // Pixel rect (x, y, w, h; top-left origin) every UI Mask / Rect Mask 2D ancestor of
    // `entity` clips it to. False when nothing clips it. The entity itself counts only when
    // `includeSelf` (a mask clips its children, not its own graphic).
    bool ResolveUIClipRect(Scene& scene, Entity entity, glm::vec4& outRect, bool includeSelf = false);

    // Product of the Canvas Group alphas above `entity` (stops at Ignore Parent Groups).
    float ResolveCanvasGroupAlpha(Scene& scene, Entity entity);

    // False when a Canvas Group above `entity` is not Interactable / does not Block Raycasts,
    // or the pointer is outside the masks clipping it -- such UI ignores the pointer.
    bool IsUIPointerAllowed(Scene& scene, Entity entity, const glm::vec2& pointer);

    // Text size for layout (Content Size Fitter): measured with the last UI renderer when one
    // has drawn already, otherwise estimated (Renderer/UIRenderer.cpp).
    glm::vec2 MeasureUITextForLayout(const UITextComponent& text);

}
