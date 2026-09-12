#pragma once

#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Dispatches world-space pointer events to scripts explicitly implementing an
    // IPointer*Handler interface, by raycasting through every enabled PhysicsRaycaster2DComponent /
    // PhysicsRaycaster3DComponent paired with a CameraComponent. Call once per frame during
    // Play, after Input is updated and before Scene::OnRuntimeUpdate -- same cadence as
    // UpdateUIInteractions.
    void UpdatePhysicsRaycasterInteractions(Scene& scene);

}
