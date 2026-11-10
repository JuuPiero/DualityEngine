#pragma once

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEditor/SceneView/SceneViewProjection.h"
#include "DualityEngine/Scene/Components.h"

// Immediate-mode (ImGui draw list) gizmos of the Scene view: camera frustums, light shapes,
// collider wireframes with their resize handles, and the 3D grid. Pure drawing / hit-testing
// helpers -- they never change the scene; the Scene panel applies the returned drags.
namespace Duality::SceneViewDraw {

    // ---- Cameras and lights (3D pane) ----------------------------------------------------
    // Frustum wireframe of a camera entity (camForward/Right/Up = that camera's own basis).
    void DrawCameraFrustum(const Projector3D& proj, const glm::vec3& camPos, const glm::vec3& camForward, const glm::vec3& camRight,
        const glm::vec3& camUp, ProjectionType projectionType, float fovDegrees, float orthoHalfHeight, float aspect, float nearPlane,
        float farPlane, ImU32 color);
    void DrawCameraBodyGizmo(const Projector3D& proj, const glm::vec3& cameraPosition, const glm::vec3& cameraForward, ImU32 color);
    void DrawDirectionalLightGizmo(const Projector3D& proj, const TransformComponent& transform);
    void DrawPointLightGizmo(const Projector3D& proj, const TransformComponent& transform, const PointLightComponent& light, bool selected);
    void DrawSpotLightGizmo(const Projector3D& proj, const TransformComponent& transform, const SpotLightComponent& light, bool selected);

    // ---- Colliders (3D pane): wireframes + screen positions of their resize handles ------
    // A handle behind the camera is reported as NaN (callers skip it that frame).
    void DrawBoxCollider3D(const Projector3D& proj, const glm::vec3& center, const glm::quat& rotation, const glm::vec3& halfExtents, ImVec2 outHandles[3]);
    void DrawSphereCollider3D(const Projector3D& proj, const glm::vec3& center, float radius, ImVec2& outHandle);
    void DrawCapsuleCollider3D(const Projector3D& proj, const glm::vec3& center, const glm::quat& rotation,
        float radius, float height, ImVec2& outRadiusHandle, ImVec2& outHeightHandle);
    // Drags one collider resize handle (an InvisibleButton named `id`); returns the new value.
    float DragCollider3DHandle(const char* id, ImVec2 centerScreen, ImVec2 handleScreen, float cameraDistance, float current);

    // ---- Colliders (2D pane) ---------------------------------------------------------------
    constexpr float Collider2DHandleHalfSize = 5.0f; // half size (px) of a 2D collider resize handle
    float DragCollider2DHandle(const char* id, ImVec2 centerScreen, ImVec2 handleScreen, float zoom, float current);
    glm::vec2 GetPolygonVertex2DConst(const PolygonCollider2DComponent& poly, int index);
    bool PointInPolygon2D(const glm::vec2& point, const PolygonCollider2DComponent& poly);
    bool PointInCapsule2D(const glm::vec2& point, const glm::vec2& center, float radius, float height);
    void DrawCapsule2DOutline(ImDrawList* drawList, const ImVec2& topCapScreen, const ImVec2& bottomCapScreen, float radiusPx, ImU32 color);

    // ---- Grid ----------------------------------------------------------------------------
    // XZ grid of +-extent around `origin` (snapped to the cell size by the caller), with the
    // world X (red) and Z (blue) axis lines, clipped to the pane.
    void DrawGrid3D(const Projector3D& proj, const glm::vec3& origin, float cellSize, float extent);

}
