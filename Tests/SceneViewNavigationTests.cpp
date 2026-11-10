// Scene view navigation (DualityEditor/SceneView/SceneViewNavigation.h): the PiaEngine / Unity
// camera rules, checked on the pure camera math the Scene view panes run every frame.
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "TestFramework.h"

#include "DualityEditor/SceneView/SceneViewNavigation.h"

using namespace Duality;
using namespace Duality::SceneViewNavigation;

namespace {
    bool Near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) <= eps; }
    bool Near(const glm::vec2& a, const glm::vec2& b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }
    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) { return glm::length(a - b) <= eps; }

    // Pixel position of world point `p` in a perspective pane (y down), like the renderer.
    glm::vec2 Project(const SceneViewCamera3D& camera, const glm::vec3& p, const glm::vec2& viewport) {
        const glm::vec3 eye = camera.Position(), rel = p - eye;
        const float z = glm::dot(rel, camera.Forward());
        const float t = std::tan(glm::radians(FovDegrees) * 0.5f);
        const float aspect = viewport.x / viewport.y;
        const float ndcX = glm::dot(rel, camera.Right()) / (z * t * aspect);
        const float ndcY = glm::dot(rel, camera.Up()) / (z * t);
        return { (ndcX * 0.5f + 0.5f) * viewport.x, (0.5f - ndcY * 0.5f) * viewport.y };
    }

    SceneViewCamera3D MakeCamera3D() {
        SceneViewCamera3D camera;
        camera.Target = { 1.0f, 2.0f, -3.0f };
        camera.Distance = 10.0f;
        camera.Yaw = 0.3f;
        camera.Pitch = -0.4f;
        camera.SyncRotation();
        return camera;
    }
}

TEST_CASE("Scene view: mouse buttons map to Unity / PiaEngine navigation modes") {
    CHECK_SOFT(PressMode3D(2, false, false, false) == Drag3D::Pan, "MMB pans");
    CHECK_SOFT(PressMode3D(1, false, false, false) == Drag3D::Fly, "RMB flies");
    CHECK_SOFT(PressMode3D(1, true, false, false) == Drag3D::Dolly, "Alt+RMB dollies");
    CHECK_SOFT(PressMode3D(0, true, false, false) == Drag3D::Orbit, "Alt+LMB orbits");
    CHECK_SOFT(PressMode3D(0, false, true, false) == Drag3D::Pan, "Hand tool LMB pans");
    CHECK_SOFT(PressMode3D(0, false, false, false) == Drag3D::Select, "LMB selects");
    CHECK_SOFT(PressMode3D(1, false, false, true) == Drag3D::Pan && PressMode3D(0, true, false, true) == Drag3D::Pan, "flat (2D) view pans instead of fly/orbit");
    CHECK_SOFT(PressMode2D(1, false, false) == Drag2D::Pan && PressMode2D(2, false, false) == Drag2D::Pan, "2D: RMB/MMB pan");
    CHECK_SOFT(PressMode2D(0, true, false) == Drag2D::Pan && PressMode2D(0, false, true) == Drag2D::Pan, "2D: Alt+LMB / Hand pan");
    CHECK_SOFT(PressMode2D(0, false, false) == Drag2D::Select, "2D: LMB selects");
}

TEST_CASE("Scene view 2D: wheel zooms toward the cursor, pan follows the mouse, F animates") {
    const float ppu = 100.0f;
    const glm::vec2 viewport{ 800.0f, 480.0f };
    SceneViewCamera camera;
    camera.Position = { 3.0f, -1.0f };
    camera.Zoom = 1.0f;
    const glm::vec2 cursor{ 650.0f, 90.0f };
    const glm::vec2 before = ScreenToWorld2D(camera, cursor, viewport, ppu);
    const float sizeBefore = OrthoSize2D(camera, viewport.y, ppu);
    ZoomAtCursor2D(camera, 1.0f, cursor, viewport, ppu);
    CHECK_SOFT(Near(OrthoSize2D(camera, viewport.y, ppu), sizeBefore / 1.15f), "one notch = orthoSize / 1.15");
    CHECK_SOFT(Near(ScreenToWorld2D(camera, cursor, viewport, ppu), before), "world point under the cursor stays under it (zoom in)");
    ZoomAtCursor2D(camera, -3.0f, cursor, viewport, ppu);
    CHECK_SOFT(Near(ScreenToWorld2D(camera, cursor, viewport, ppu), before), "... and zooming out");
    for (int i = 0; i < 200; ++i)
        ZoomAtCursor2D(camera, -1.0f, cursor, viewport, ppu);
    CHECK_SOFT(Near(OrthoSize2D(camera, viewport.y, ppu), 5000.0f, 0.5f), "orthoSize clamps at 5000");

    camera.Zoom = 1.0f;
    PaneInput input;
    input.ViewportSize = viewport;
    input.MousePos = { 200.0f, 200.0f };
    const glm::vec2 grabbed = ScreenToWorld2D(camera, input.MousePos, viewport, ppu);
    input.MouseDelta = { 37.0f, -12.0f };
    Navigate2D(camera, Drag2D::Pan, input, ppu);
    CHECK_SOFT(Near(ScreenToWorld2D(camera, input.MousePos + input.MouseDelta, viewport, ppu), grabbed), "pan: the grabbed world point follows the mouse");

    Frame2D(camera, { -2.0f, -1.0f }, { 2.0f, 1.0f }, viewport, ppu);
    Animate2D(camera, 0.1f);
    CHECK_SOFT(camera.Anim > 0.0f && !Near(camera.Position, { 0.0f, 0.0f }), "frame is animated, not instant");
    for (int i = 0; i < 10; ++i)
        Animate2D(camera, 0.05f);
    CHECK_SOFT(camera.Anim < 0.0f && Near(camera.Position, { 0.0f, 0.0f }), "frame ends on the selection center after ~0.25 s");
    // size = max(h/2, w/2/aspect, 0.5) * 1.4 = max(1, 2/(800/480), 0.5) * 1.4 = 1.68
    CHECK_SOFT(Near(OrthoSize2D(camera, viewport.y, ppu), 1.68f), "frame size = max(h/2, w/2/aspect, 0.5) * 1.4");
}

TEST_CASE("Scene view 3D: orbit, pan, fly, dolly and wheel follow the PiaEngine rules") {
    const glm::vec2 viewport{ 800.0f, 480.0f };
    PaneInput input;
    input.ViewportSize = viewport;

    SceneViewCamera3D camera = MakeCamera3D();
    input.MouseDelta = { 50.0f, 20.0f };
    Navigate3D(camera, Drag3D::Orbit, input);
    CHECK_SOFT(Near(camera.Yaw, 0.3f - 50.0f * 0.006f) && Near(camera.Pitch, -0.4f - 20.0f * 0.006f), "orbit: 0.006 rad/px");
    CHECK_SOFT(Near(camera.Target, { 1.0f, 2.0f, -3.0f }) && Near(camera.Distance, 10.0f), "orbit keeps pivot and distance");
    input.MouseDelta = { 0.0f, 10000.0f };
    Navigate3D(camera, Drag3D::Orbit, input);
    CHECK_SOFT(Near(camera.Pitch, -glm::half_pi<float>()), "pitch clamps at -90 degrees");

    camera = MakeCamera3D();
    const glm::vec3 pivot = camera.Target;
    const glm::vec2 pivotScreen = Project(camera, pivot, viewport);
    input.MouseDelta = { 40.0f, -25.0f };
    Navigate3D(camera, Drag3D::Pan, input);
    CHECK_SOFT(Near(Project(camera, pivot, viewport), pivotScreen + input.MouseDelta, 0.05f), "pan: the point at the pivot follows the mouse");

    camera = MakeCamera3D();
    const glm::vec3 eye = camera.Position();
    input.MouseDelta = { 30.0f, 10.0f };
    Navigate3D(camera, Drag3D::Fly, input);
    CHECK_SOFT(Near(camera.Position(), eye) && Near(camera.Yaw, 0.3f - 30.0f * 0.004f), "fly look: 0.004 rad/px around a fixed eye");
    input.MouseDelta = { 0.0f, 0.0f };
    input.W = true;
    input.DeltaTime = 0.5f;
    const glm::vec3 forward = camera.Forward();
    const glm::vec3 eyeBefore = camera.Position();
    Navigate3D(camera, Drag3D::Fly, input);
    CHECK_SOFT(Near(camera.Position() - eyeBefore, forward * (10.0f * 1.0f * 0.5f)), "W moves forward at max(2, distance) * flySpeed");
    input.W = false;
    input.Shift = true;
    input.D = true;
    const glm::vec3 right = camera.Right();
    const glm::vec3 eyeBeforeShift = camera.Position();
    Navigate3D(camera, Drag3D::Fly, input);
    CHECK_SOFT(Near(camera.Position() - eyeBeforeShift, right * (10.0f * 3.0f * 0.5f)), "Shift = x3");
    input.D = input.Shift = false;
    input.Wheel = 2.0f;
    Navigate3D(camera, Drag3D::Fly, input);
    CHECK_SOFT(Near(camera.FlySpeed, 1.44f) && Near(camera.Distance, 10.0f), "wheel while flying changes fly speed (x1.2/notch), not the zoom");
    input.Wheel = 100.0f;
    Navigate3D(camera, Drag3D::Fly, input);
    CHECK_SOFT(Near(camera.FlySpeed, 20.0f), "fly speed clamps at 20");

    camera = MakeCamera3D();
    input.Wheel = 0.0f;
    input.MouseDelta = { 30.0f, -10.0f };
    Navigate3D(camera, Drag3D::Dolly, input);
    CHECK_SOFT(Near(camera.Distance, 10.0f * std::exp(40.0f * 0.005f)), "Alt+RMB dolly: distance *= exp((dx - dy) * 0.005)");

    camera = MakeCamera3D();
    input.MouseDelta = { 0.0f, 0.0f };
    input.Wheel = -1.0f;
    Navigate3D(camera, Drag3D::None, input);
    CHECK_SOFT(Near(camera.Distance, 11.2f), "wheel out: distance x 1.12");
    input.Wheel = 2.0f;
    Navigate3D(camera, Drag3D::None, input);
    CHECK_SOFT(Near(camera.Distance, 11.2f / (1.12f * 1.12f)), "wheel in: distance / 1.12 per notch");
}

TEST_CASE("Scene view 3D: Frame Selected and axis snaps animate; ortho grid hides when flat") {
    SceneViewCamera3D camera = MakeCamera3D();
    Frame3D(camera, { 5.0f, 0.0f, 0.0f }, 2.0f);
    Animate3D(camera, 0.1f);
    CHECK_SOFT(camera.Anim > 0.0f && !Near(camera.Target, { 5.0f, 0.0f, 0.0f }), "frame is animated");
    while (Animate3D(camera, 0.05f)) {}
    CHECK_SOFT(Near(camera.Target, { 5.0f, 0.0f, 0.0f }) && Near(camera.Distance, 2.0f / std::sin(glm::radians(30.0f)) * 1.05f), "distance = radius / sin(fov/2) * 1.05");
    Frame3D(camera, { 0.0f, 0.0f, 0.0f }, 0.01f);
    while (Animate3D(camera, 0.05f)) {}
    CHECK_SOFT(Near(camera.Distance, 0.5f / std::sin(glm::radians(30.0f)) * 1.05f), "radius >= 0.5");

    SnapView3D(camera, glm::half_pi<float>(), 0.0f, true);
    while (Animate3D(camera, 0.05f)) {}
    CHECK_SOFT(camera.Orthographic && Near(camera.Forward(), { -1.0f, 0.0f, 0.0f }), "X cone: orthographic view from +X");
    SnapView3D(camera, 0.0f, -glm::half_pi<float>(), true);
    while (Animate3D(camera, 0.05f)) {}
    CHECK_SOFT(Near(camera.Forward(), { 0.0f, -1.0f, 0.0f }), "Y cone: top view");
    CHECK_SOFT(GridVisible3D(camera), "top ortho view shows the grid");
    SnapView3D(camera, 0.0f, 0.0f, true);
    while (Animate3D(camera, 0.05f)) {}
    CHECK_SOFT(!GridVisible3D(camera), "front ortho view (flat on the grid) hides it");
    camera.Orthographic = false;
    CHECK_SOFT(GridVisible3D(camera), "perspective always shows the grid");
    camera.Distance = 100.0f;
    CHECK_SOFT(GridCellSize3D(camera) == 10.0f, "coarse grid when far");
    const glm::vec3 origin = GridOrigin3D(camera);
    CHECK_SOFT(std::fmod(std::abs(origin.x), 10.0f) < 1e-3f && origin.y == 0.0f, "grid origin snapped to its spacing");
}
