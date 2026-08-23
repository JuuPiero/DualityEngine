#pragma once

namespace Duality {

    struct EditorContext;

    // Unity-style Scene view, split into two side-by-side panes (Top/Bottom
    // screen) since the 3DS has two independent physical screens -- each pane
    // is its own free-roam editor-only camera (SceneViewCamera, no
    // CameraComponent mutation involved), seeded once from that screen's real
    // primary camera so the initial view looks WYSIWYG. Each pane renders into
    // its own framebuffer, resized every frame to match (like a real editor
    // viewport, not a fixed-size canvas). Middle-drag pans, mouse wheel zooms,
    // left-click selects a sprite or drags the selected entity's gizmo --
    // selection and the active gizmo tool are shared across both panes.
    class ScenePanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
