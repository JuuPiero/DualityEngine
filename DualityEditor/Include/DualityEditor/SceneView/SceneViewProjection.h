#pragma once

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEngine/ECS/Entity.h"

// Camera math shared by the Scene view's 3D pane, its gizmo drawing (SceneGizmoDrawing.h) and
// its overlays (SceneViewOverlays.h): world <-> pane-pixel projection, the orbit camera's
// Euler <-> quaternion conversions and the screen-size rules of the transform handles.
namespace Duality::SceneViewDraw {

        // Projects world-space points into this pane's screen-space pixels, given the orbit
        // camera's own basis vectors -- the exact inverse of the pick-ray math in
        // DrawScenePane3D's click handler (same forward/right/up, fov, aspect). Shared by the
        // 3D gizmo's drawing, hit-testing, and drag math below.
        struct Projector3D {
            glm::vec3 CameraPos, Forward, Right, Up;
            float FovDegrees, Aspect;
            ImVec2 ImagePos;
            int ViewportW, ViewportH;
            // Orthographic Scene view (axis-gizmo Iso): parallel rays, OrthoHalfHeight world units.
            bool Ortho = false;
            float OrthoHalfHeight = 1.0f;

            bool Project(const glm::vec3& worldPos, ImVec2& outScreen) const {
                glm::vec3 rel = worldPos - CameraPos;
                // Forward-facing depth: positive and growing for a point further in front of
                // the camera (rel increasingly parallel to Forward) -- NOT negated, matching
                // the pick-ray code's own dot(toSphere, rayDir) > 0 "in front" convention below.
                float viewZ = glm::dot(rel, Forward);
                if (viewZ <= 0.01f)
                    return false; // behind the camera
                float ndcX, ndcY;
                if (Ortho) {
                    ndcX = glm::dot(rel, Right) / (OrthoHalfHeight * Aspect);
                    ndcY = glm::dot(rel, Up) / OrthoHalfHeight;
                } else {
                    float tanHalfFov = std::tan(glm::radians(FovDegrees) * 0.5f);
                    ndcX = glm::dot(rel, Right) / (viewZ * tanHalfFov * Aspect);
                    ndcY = glm::dot(rel, Up) / (viewZ * tanHalfFov);
                }
                outScreen = ImVec2(ImagePos.x + (ndcX * 0.5f + 0.5f) * ViewportW, ImagePos.y + (0.5f - ndcY * 0.5f) * ViewportH);
                return true;
            }

            // The world ray under a screen point (picking, asset drops).
            void Ray(const ImVec2& screen, glm::vec3& outOrigin, glm::vec3& outDirection) const {
                const float ndcX = (2.0f * ((screen.x - ImagePos.x) / ViewportW)) - 1.0f;
                const float ndcY = 1.0f - (2.0f * ((screen.y - ImagePos.y) / ViewportH));
                if (Ortho) {
                    outOrigin = CameraPos + Right * (ndcX * OrthoHalfHeight * Aspect) + Up * (ndcY * OrthoHalfHeight);
                    outDirection = Forward;
                    return;
                }
                const float tanHalfFov = std::tan(glm::radians(FovDegrees) * 0.5f);
                outOrigin = CameraPos;
                outDirection = glm::normalize(Forward + Right * (ndcX * tanHalfFov * Aspect) + Up * (ndcY * tanHalfFov));
            }
        };

        // Transform handles are an editor overlay, so their visual size is measured in screen
        // pixels rather than world units. A fixed 60-unit gizmo dwarfs normal PPU sprites (a
        // 128px texture may be only 1.28 world units), as well as becoming unusable at range.
        constexpr float Gizmo3DTargetPixels = 52.0f;
        constexpr float Gizmo3DHitBand = 8.0f;
        constexpr float Gizmo3DScaleHandleHalfSize = 5.0f;
        constexpr float Gizmo3DCenterRadius = 6.0f;


        // Camera-local forward of an orbit camera at (pitch, yaw, 0) degrees -- the same
        // Rz*Ry*Rx order as the renderers' ComposeWorldMtx.
        glm::vec3 OrbitForward(float pitchDegrees, float yawDegrees);
        // Euler degrees (Rz*Ry*Rx) -> quaternion, and its ZYX Tait-Bryan inverse (only used to
        // feed IRenderer3D::BeginScene's Euler-degrees camera rotation).
        glm::quat EulerDegreesToQuat(const glm::vec3& rotationDegrees);
        glm::vec3 QuatToEulerDegrees(const glm::quat& q);
        // Bounding-sphere radius (before Transform scale) of what an entity's Mesh Filter holds.
        float MeshBoundingRadius(Entity entity);
        // Pixel distance from `p` to the segment a-b.
        float PointSegmentDistance(const ImVec2& p, const ImVec2& a, const ImVec2& b);
        // World length that makes a transform handle at `originWorld` Gizmo3DTargetPixels long.
        float Gizmo3DWorldAxisLength(const Projector3D& proj, const glm::vec3& originWorld);

}
