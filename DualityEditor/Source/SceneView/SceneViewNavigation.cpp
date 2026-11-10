#include "DualityEditor/SceneView/SceneViewNavigation.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Duality::SceneViewNavigation {

    namespace {
        constexpr float HalfPi = glm::half_pi<float>();
        constexpr float MinDistance = 0.05f;
        constexpr float MaxDistance = 100000.0f;
        constexpr float AnimSeconds = 0.25f;

        float EaseOutCubic(float t) { return 1.0f - std::pow(1.0f - t, 3.0f); }
        float ClampPitch(float pitch) { return std::clamp(pitch, -HalfPi, HalfPi); }

        // Shortest signed angle from `from` to `to` (radians).
        float AngleDelta(float from, float to) {
            float d = std::fmod(to - from, glm::two_pi<float>());
            if (d > glm::pi<float>()) d -= glm::two_pi<float>();
            if (d < -glm::pi<float>()) d += glm::two_pi<float>();
            return d;
        }
    }

    // ---------------------------------------------------------------------------------- 3D

    Drag3D PressMode3D(int button, bool alt, bool handTool, bool flatView) {
        if (button == 2)
            return Drag3D::Pan;
        if (button == 1)
            return alt ? Drag3D::Dolly : (flatView ? Drag3D::Pan : Drag3D::Fly);
        if (button == 0) {
            if (alt)
                return flatView ? Drag3D::Pan : Drag3D::Orbit;
            return handTool ? Drag3D::Pan : Drag3D::Select;
        }
        return Drag3D::None;
    }

    bool Navigate3D(SceneViewCamera3D& camera, Drag3D mode, const PaneInput& input) {
        bool moved = false;
        const glm::vec2 d = input.MouseDelta;
        const bool dragging = d.x != 0.0f || d.y != 0.0f;
        if (mode != Drag3D::None && mode != Drag3D::Select && (dragging || mode == Drag3D::Fly))
            camera.Anim = -1.0f; // any navigation cancels a Frame Selected animation
        switch (mode) {
            case Drag3D::Orbit:
                if (dragging) {
                    camera.Yaw -= d.x * 0.006f;
                    camera.Pitch = ClampPitch(camera.Pitch - d.y * 0.006f);
                    camera.SyncRotation();
                    moved = true;
                }
                break;
            case Drag3D::Fly: {
                if (dragging) {
                    // Look around the camera position, which stays fixed.
                    const glm::vec3 eye = camera.Position();
                    camera.Yaw -= d.x * 0.004f;
                    camera.Pitch = ClampPitch(camera.Pitch - d.y * 0.004f);
                    camera.SyncRotation();
                    camera.Target = eye + camera.Forward() * camera.Distance;
                    moved = true;
                }
                glm::vec3 local(0.0f);
                if (input.W) local.z -= 1.0f;
                if (input.S) local.z += 1.0f;
                if (input.A) local.x -= 1.0f;
                if (input.D) local.x += 1.0f;
                if (input.E) local.y += 1.0f;
                if (input.Q) local.y -= 1.0f;
                if (glm::dot(local, local) > 0.0f) {
                    const float speed = std::max(2.0f, camera.Distance) * camera.FlySpeed * (input.Shift ? 3.0f : 1.0f);
                    camera.Target += camera.Rotation * glm::normalize(local) * speed * input.DeltaTime;
                    moved = true;
                }
                // The wheel changes the fly speed instead of zooming while flying.
                if (input.Wheel != 0.0f)
                    camera.FlySpeed = std::clamp(camera.FlySpeed * std::pow(1.2f, input.Wheel), 0.05f, 20.0f);
                return moved;
            }
            case Drag3D::Pan:
                if (dragging) {
                    // The point under the cursor (at the pivot depth) follows the mouse.
                    const float k = 2.0f * camera.Distance * std::tan(glm::radians(FovDegrees) * 0.5f) / std::max(1.0f, input.ViewportSize.y);
                    camera.Target += camera.Right() * (-d.x * k) + camera.Up() * (d.y * k);
                    moved = true;
                }
                break;
            case Drag3D::Dolly:
                if (dragging) {
                    camera.Distance = std::clamp(camera.Distance * std::exp((d.x - d.y) * 0.005f), MinDistance, MaxDistance);
                    moved = true;
                }
                break;
            default:
                break;
        }
        if (input.Wheel != 0.0f) {
            camera.Anim = -1.0f;
            camera.Distance = std::clamp(camera.Distance * std::pow(1.12f, -input.Wheel), MinDistance, MaxDistance);
            moved = true;
        }
        return moved;
    }

    void Frame3D(SceneViewCamera3D& camera, const glm::vec3& center, float radius) {
        const float r = std::max(radius, 0.5f);
        camera.AnimFromTarget = camera.Target;
        camera.AnimToTarget = center;
        camera.AnimFromDistance = camera.Distance;
        camera.AnimToDistance = std::clamp(r / std::sin(glm::radians(FovDegrees) * 0.5f) * 1.05f, MinDistance, MaxDistance);
        camera.AnimFromYaw = camera.AnimToYaw = camera.Yaw;
        camera.AnimFromPitch = camera.AnimToPitch = camera.Pitch;
        camera.Anim = 0.0f;
    }

    void SnapView3D(SceneViewCamera3D& camera, float yaw, float pitch, bool orthographic) {
        camera.AnimFromTarget = camera.AnimToTarget = camera.Target;
        camera.AnimFromDistance = camera.AnimToDistance = camera.Distance;
        camera.AnimFromYaw = camera.Yaw;
        camera.AnimToYaw = camera.Yaw + AngleDelta(camera.Yaw, yaw);
        camera.AnimFromPitch = camera.Pitch;
        camera.AnimToPitch = ClampPitch(pitch);
        camera.Orthographic = orthographic;
        camera.Anim = 0.0f;
    }

    bool Animate3D(SceneViewCamera3D& camera, float deltaTime) {
        if (camera.Anim < 0.0f)
            return false;
        camera.Anim = std::min(1.0f, camera.Anim + deltaTime / AnimSeconds);
        const float k = EaseOutCubic(camera.Anim);
        camera.Target = glm::mix(camera.AnimFromTarget, camera.AnimToTarget, k);
        camera.Distance = camera.AnimFromDistance + (camera.AnimToDistance - camera.AnimFromDistance) * k;
        camera.Yaw = camera.AnimFromYaw + (camera.AnimToYaw - camera.AnimFromYaw) * k;
        camera.Pitch = camera.AnimFromPitch + (camera.AnimToPitch - camera.AnimFromPitch) * k;
        camera.SyncRotation();
        if (camera.Anim >= 1.0f)
            camera.Anim = -1.0f;
        return true;
    }

    float OrthoHalfHeight(const SceneViewCamera3D& camera) {
        return camera.Distance * std::tan(glm::radians(FovDegrees) * 0.5f);
    }

    bool GridVisible3D(const SceneViewCamera3D& camera) {
        return !(camera.Orthographic && std::abs(camera.Pitch) < 0.05f);
    }

    float GridCellSize3D(const SceneViewCamera3D& camera) {
        return camera.Distance > 45.0f ? 10.0f : 1.0f;
    }

    glm::vec3 GridOrigin3D(const SceneViewCamera3D& camera) {
        // Follows the camera, snapped to the grid spacing so lines don't swim.
        const float snap = GridCellSize3D(camera);
        const glm::vec3 focus = camera.Orthographic ? camera.Target : glm::mix(camera.Position(), camera.Target, 0.6f);
        return { std::round(focus.x / snap) * snap, 0.0f, std::round(focus.z / snap) * snap };
    }

    // ---------------------------------------------------------------------------------- 2D

    Drag2D PressMode2D(int button, bool alt, bool handTool) {
        if (button == 1 || button == 2 || (button == 0 && (alt || handTool)))
            return Drag2D::Pan;
        return button == 0 ? Drag2D::Select : Drag2D::None;
    }

    float OrthoSize2D(const SceneViewCamera& camera, float viewportHeight, float ppu) {
        return viewportHeight * 0.5f / std::max(1e-6f, camera.Zoom * ppu);
    }

    glm::vec2 ScreenToWorld2D(const SceneViewCamera& camera, const glm::vec2& mousePos, const glm::vec2& viewportSize, float ppu) {
        const float pixelsPerUnit = camera.Zoom * ppu;
        return (mousePos - viewportSize * 0.5f) / pixelsPerUnit + camera.Position;
    }

    void ZoomAtCursor2D(SceneViewCamera& camera, float wheel, const glm::vec2& mousePos, const glm::vec2& viewportSize, float ppu) {
        if (wheel == 0.0f || viewportSize.y <= 0.0f)
            return;
        camera.Anim = -1.0f;
        const glm::vec2 before = ScreenToWorld2D(camera, mousePos, viewportSize, ppu);
        const float orthoSize = std::clamp(OrthoSize2D(camera, viewportSize.y, ppu) * std::pow(1.15f, -wheel), 0.05f, 5000.0f);
        camera.Zoom = viewportSize.y * 0.5f / (orthoSize * ppu);
        // Re-center so `before` is still under the cursor.
        camera.Position = before - (mousePos - viewportSize * 0.5f) / (camera.Zoom * ppu);
    }

    bool Navigate2D(SceneViewCamera& camera, Drag2D mode, const PaneInput& input, float ppu) {
        bool moved = false;
        if (mode == Drag2D::Pan && (input.MouseDelta.x != 0.0f || input.MouseDelta.y != 0.0f)) {
            camera.Anim = -1.0f;
            camera.Position -= input.MouseDelta / (camera.Zoom * ppu); // world point under the cursor follows it
            moved = true;
        }
        if (input.Wheel != 0.0f) {
            ZoomAtCursor2D(camera, input.Wheel, input.MousePos, input.ViewportSize, ppu);
            moved = true;
        }
        return moved;
    }

    void Frame2D(SceneViewCamera& camera, const glm::vec2& min, const glm::vec2& max, const glm::vec2& viewportSize, float ppu) {
        const float aspect = viewportSize.x / std::max(1.0f, viewportSize.y);
        const float size = std::max({ (max.y - min.y) * 0.5f, (max.x - min.x) * 0.5f / aspect, 0.5f }) * 1.4f;
        camera.AnimFromPosition = camera.Position;
        camera.AnimToPosition = (min + max) * 0.5f;
        camera.AnimFromZoom = camera.Zoom;
        camera.AnimToZoom = viewportSize.y * 0.5f / (std::clamp(size, 0.05f, 5000.0f) * ppu);
        camera.Anim = 0.0f;
    }

    bool Animate2D(SceneViewCamera& camera, float deltaTime) {
        if (camera.Anim < 0.0f)
            return false;
        camera.Anim = std::min(1.0f, camera.Anim + deltaTime / AnimSeconds);
        const float k = EaseOutCubic(camera.Anim);
        camera.Position = glm::mix(camera.AnimFromPosition, camera.AnimToPosition, k);
        // Interpolate the visible size (orthoSize = 1/Zoom), not the zoom factor itself.
        const float fromSize = 1.0f / camera.AnimFromZoom, toSize = 1.0f / camera.AnimToZoom;
        camera.Zoom = 1.0f / (fromSize + (toSize - fromSize) * k);
        if (camera.Anim >= 1.0f)
            camera.Anim = -1.0f;
        return true;
    }

}
