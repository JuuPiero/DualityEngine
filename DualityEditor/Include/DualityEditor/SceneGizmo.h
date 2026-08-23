#pragma once

#include <imgui.h>

namespace Duality {

    // Which gizmo tool is active in the Scene view -- Q/W/E/R-style tool
    // switching, matching Unity/MyGameEngine (this project skips a "None"
    // mode -- Translate is always at least available).
    enum class GizmoMode {
        Translate,
        Rotate,
        Scale
    };

    // Which handle of the active gizmo is under the mouse (or being
    // dragged). For Translate/Scale, Both is the free-move/free-scale
    // center square (drags X and Y at once); for Rotate there is only ever
    // one handle (the ring), reported as Both too so callers don't need a
    // separate "is dragging" flag per mode. Z is the 3D Scene view pane's
    // own third axis (see ScenePanel.cpp's DrawAndHitTestGizmo3D) -- the 2D
    // gizmo above never produces or consumes it.
    enum class GizmoAxis {
        None,
        X,
        Y,
        Z,
        Both
    };

    // Draws whichever 2D gizmo `mode` selects, centered at `origin`
    // (absolute screen-space pixels, e.g. the selected entity's world
    // position run through the Scene view's world-to-screen transform),
    // into the *current* ImGui window's draw list -- call between that
    // window's Begin/End. `activeAxis` (None if nothing is being dragged)
    // is drawn highlighted, so a handle stays lit for the whole drag even
    // if the mouse drifts off it mid-drag. Returns whichever handle the
    // mouse is currently over, for the caller to decide whether a click
    // should start a drag; the caller is responsible for actually applying
    // the drag to Transform's Translation/Rotation/Scale (this function
    // only draws and hit-tests, it never touches a TransformComponent).
    GizmoAxis DrawAndHitTestGizmo2D(GizmoMode mode, const ImVec2& origin, GizmoAxis activeAxis);

}
