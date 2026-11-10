#pragma once

#include <glm/glm.hpp>

#include "DualityEditor/SceneViewCamera.h"
#include "DualityEditor/SceneViewCamera3D.h"

namespace Duality {

    // Scene view camera navigation, the same controls as the PiaEngine / Unity Scene view.
    // Pure math on the pane cameras (no ImGui), so it is unit-tested (Tests/EditorLogicTests)
    // and driven by ScenePanel with the pane's mouse / keyboard state each frame.
    namespace SceneViewNavigation {

        // ---- Input of one pane for one frame ---------------------------------------------------
        enum class Drag3D { None, Select, Orbit, Pan, Fly, Dolly };
        enum class Drag2D { None, Select, Pan };

        struct PaneInput {
            glm::vec2 MousePos{ 0.0f };      // relative to the pane image (pixels, y down)
            glm::vec2 MouseDelta{ 0.0f };    // pixels since last frame
            glm::vec2 ViewportSize{ 1.0f };  // pane image size in pixels
            float Wheel = 0.0f;              // notches, > 0 = away from the user (zoom in)
            float DeltaTime = 0.0f;
            bool Alt = false, Shift = false;
            // Fly keys while the right button is held.
            bool W = false, A = false, S = false, D = false, Q = false, E = false;
        };

        // ---- 3D pane (orbit camera: pivot + distance + yaw/pitch) ------------------------------
        inline constexpr float FovDegrees = 60.0f;

        // Which drag a mouse press starts (PiaEngine ThreeSceneView rules): MMB pan; RMB fly, or
        // Alt+RMB dolly, or pan in 2D-style views; Alt+LMB orbit (pan in 2D-style views); the
        // Hand tool pans with LMB; otherwise LMB selects / marquees / drags gizmos.
        Drag3D PressMode3D(int button, bool alt, bool handTool, bool flatView);

        // Applies one frame of `mode` (and the wheel) to the camera. Returns true when the camera
        // moved (the caller saves the per-scene view state when the drag ends).
        bool Navigate3D(SceneViewCamera3D& camera, Drag3D mode, const PaneInput& input);

        // Starts the ~0.25 s eased fly-to of Frame Selected (F) around a bounding sphere.
        void Frame3D(SceneViewCamera3D& camera, const glm::vec3& center, float radius);
        // Snaps to an axis view (axis gizmo click), animated; `orthographic` switches projection.
        void SnapView3D(SceneViewCamera3D& camera, float yaw, float pitch, bool orthographic);
        // Advances an animation started by Frame3D / SnapView3D. Returns true while animating.
        bool Animate3D(SceneViewCamera3D& camera, float deltaTime);

        // Orthographic half height that shows what the perspective view shows at the pivot.
        float OrthoHalfHeight(const SceneViewCamera3D& camera);
        // Grid: hidden when looking flat at the XZ plane in an orthographic view.
        bool GridVisible3D(const SceneViewCamera3D& camera);
        // Grid cell size for the current zoom (1 near, 10 far) and the snapped grid origin.
        float GridCellSize3D(const SceneViewCamera3D& camera);
        glm::vec3 GridOrigin3D(const SceneViewCamera3D& camera);

        // ---- 2D pane (orthographic: Position = view center in world units, Zoom) ---------------
        // pixelsPerUnit = camera.Zoom * PPU; orthoSize = viewportHeight / 2 / pixelsPerUnit.
        Drag2D PressMode2D(int button, bool alt, bool handTool);
        bool Navigate2D(SceneViewCamera& camera, Drag2D mode, const PaneInput& input, float ppu);
        // Wheel zoom toward the cursor: orthoSize *= 1.15 per notch (0.05..5000 world units),
        // re-centered so the world point under the cursor stays under it.
        void ZoomAtCursor2D(SceneViewCamera& camera, float wheel, const glm::vec2& mousePos, const glm::vec2& viewportSize, float ppu);
        // Animated frame of a world rect: size = max(h/2, w/2/aspect, 0.5) * 1.4.
        void Frame2D(SceneViewCamera& camera, const glm::vec2& min, const glm::vec2& max, const glm::vec2& viewportSize, float ppu);
        bool Animate2D(SceneViewCamera& camera, float deltaTime);
        // Screen <-> world for the 2D pane (y down in both).
        glm::vec2 ScreenToWorld2D(const SceneViewCamera& camera, const glm::vec2& mousePos, const glm::vec2& viewportSize, float ppu);
        float OrthoSize2D(const SceneViewCamera& camera, float viewportHeight, float ppu);
    }

}
