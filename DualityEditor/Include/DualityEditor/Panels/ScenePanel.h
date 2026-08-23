#pragma once

namespace Duality {

    struct EditorContext;

    // Unity-style Scene view: a free-roam editor-only camera over the
    // whole scene (no CameraComponent involved), rendered into its own
    // framebuffer that's resized every frame to exactly match the panel
    // (like a real editor viewport, not a fixed-size canvas). Middle-drag
    // pans, mouse wheel zooms, left-click selects a sprite or drags the
    // selected entity's translate gizmo.
    class ScenePanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
