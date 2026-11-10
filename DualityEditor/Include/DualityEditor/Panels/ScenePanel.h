#pragma once

#include <string>

#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    struct EditorContext;

    // Unity-style Scene view, split into two side-by-side panes (Top/Bottom screen) since the
    // 3DS has two independent physical screens -- each pane is its own free-roam editor-only
    // camera (no CameraComponent mutation), seeded once from that screen's primary camera, with
    // its own 2D/3D toggle, Grid / Gizmos toggles and a floating tools overlay. Selection and
    // the active tool are shared across both panes; cameras are remembered per scene.
    //
    // Controls (PiaEngine / Unity, SceneView/SceneViewNavigation.h): click / Ctrl- / Shift-click
    // select, drag = selection rectangle; Q/W/E/R/T tools; 3D: MMB pan, RMB fly + WASD/QE
    // (Shift x3, wheel = fly speed), Alt+LMB orbit, Alt+RMB dolly, wheel zoom, axis gizmo
    // (click = view snap / Persp-Iso, drag = orbit); 2D: MMB / RMB / Alt+LMB / Hand pan, wheel
    // zooms toward the cursor; F frames the selection (animated). The splitter can be dragged.
    class ScenePanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        float m_SplitRatio = 0.5f;
        // Scene whose saved Scene view cameras were restored (SceneViewState).
        std::string m_ViewStateScene;
    };

    // JSON snapshot of one Scene pane for the editor automation (`print-sceneview top`): image
    // rect, 2D/3D camera state and the world point under the mouse.
    std::string DescribeSceneView(EditorContext& ctx, Screen screen);

}
