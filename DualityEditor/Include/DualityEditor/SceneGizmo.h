#pragma once

#include <imgui.h>

namespace Duality {

    // Which handle of the Scene view's 2D translate gizmo is under the
    // mouse (or being dragged) -- Both is the free-move center square
    // (drags X and Y at once), matching Unity's 2D Move tool.
    enum class GizmoAxis {
        None,
        X,
        Y,
        Both
    };

    // Draws a 2D translate gizmo (X/Y arrows + a free-move center square)
    // centered at `origin` (absolute screen-space pixels, e.g. the
    // selected entity's world position run through the Scene view's
    // world-to-screen transform) into the *current* ImGui window's draw
    // list -- call between that window's Begin/End. `activeAxis` (None if
    // nothing is being dragged) is drawn highlighted, so a handle stays lit
    // for the whole drag even if the mouse drifts off it mid-drag. Returns
    // whichever handle the mouse is currently over, for the caller to
    // decide whether a click should start a drag.
    GizmoAxis DrawAndHitTestGizmo2D(const ImVec2& origin, GizmoAxis activeAxis);

}
