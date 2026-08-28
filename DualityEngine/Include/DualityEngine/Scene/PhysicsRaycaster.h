#pragma once

#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Dispatches world-space pointer events (OnPointerDown/Up/Click/Enter/Exit on Behaviour
    // scripts) by raycasting through every enabled PhysicsRaycaster2DComponent /
    // PhysicsRaycaster3DComponent paired with a CameraComponent. Call once per frame during
    // Play, after Input is updated and before Scene::OnRuntimeUpdate -- same cadence as
    // UpdateUIInteractions.
    void UpdatePhysicsRaycasterInteractions(Scene& scene);

}
