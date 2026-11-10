#pragma once

namespace Duality {

    struct EditorContext;

    // Unity-style Scene view, split into two side-by-side panes (Top/Bottom screen) since the
    // 3DS has two independent physical screens -- each pane is its own free-roam editor-only
    // camera (no CameraComponent mutation), seeded once from that screen's primary camera, with
    // its own 2D/3D toggle. Selection and the active tool are shared across both panes.
    //
    // Unity controls: click / Ctrl-click / Shift-click select, drag on empty space (or over
    // objects) draws a selection rectangle; Q/W/E/R/T tools with handles for the whole
    // selection (Pivot/Center, Local/Global), Ctrl while dragging = increment snapping;
    // 3D: RMB look + WASD/QE fly (Shift faster, wheel = fly speed), Alt+LMB orbit, Alt+RMB and
    // wheel zoom, MMB pan; 2D: wheel zooms toward the cursor, MMB/RMB pan; F frames the
    // selection. The splitter between the panes can be dragged.
    class ScenePanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        float m_SplitRatio = 0.5f;
    };

}
