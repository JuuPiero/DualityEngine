#pragma once

#include <imgui.h>

#include "DualityEngine/Renderer/Screen.h"

namespace Duality {
    struct EditorContext;
    struct SceneViewCamera3D;
}

// Overlays drawn over a Scene pane image (PiaEngine / Unity 2021 style): the axis gizmo in the
// top-right corner of the 3D pane and the floating tools column in the top-left corner.
namespace Duality::SceneViewDraw {

    // Axis gizmo: world X/Y/Z as seen by the camera. Click an axis cone = animated snap to that
    // orthographic view, click the centre square or the Persp/Iso label = toggle perspective /
    // orthographic, drag (3 px threshold) = orbit. Returns true while it owns the mouse.
    // `pressed` / `dragged` / `pressPos` are the caller's per-pane state.
    bool DrawAndInteractOrientationGizmo3D(SceneViewCamera3D& camera3D, ImVec2 imagePos, int viewportW, bool imageHovered,
        bool& pressed, bool& dragged, ImVec2& pressPos);

    // Tools overlay (Hand / Move / Rotate / Scale / Rect). HandleToolsOverlay runs right after
    // the pane image (records the image rect, switches the tool on click) and returns true
    // while the mouse is over a button; DrawToolsOverlay draws it after every other overlay.
    bool HandleToolsOverlay(EditorContext& ctx, Screen screen, ImVec2 imagePos, ImVec2 imageSize, bool imageHovered);
    void DrawToolsOverlay(EditorContext& ctx, Screen screen);

    // Screen rect of a pane's image as recorded by HandleToolsOverlay this frame.
    ImVec2 PaneImageOrigin(Screen screen);
    ImVec2 PaneImageSize(Screen screen);

}
