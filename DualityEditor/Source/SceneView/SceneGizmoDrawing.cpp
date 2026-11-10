#include "DualityEditor/SceneView/SceneGizmoDrawing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEngine/Asset/MeshReference.h"
#include "DualityEngine/Scene/Components.h"

// Moved out of ScenePanel.cpp unchanged (SceneGizmoDrawing.h / SceneViewProjection.h).
namespace Duality::SceneViewDraw {

        // Camera-local forward direction for an orbit camera at (pitchDegrees, yawDegrees, 0)
        // -- must match ComposeWorldMtx's own Rx-then-Ry-then-Rz composition order (Citro3DRenderer.
        // cpp / OpenGLRenderer3D.cpp), i.e. this is Rz(0)*Ry(yaw)*Rx(pitch) applied to a base
        // local-forward of (0,0,-1) (OpenGL's own looking-down--Z convention). Only used by this
        // desktop-editor-only orbit camera (never serialized, never shared with a real
        // CameraComponent/device), so there's no cross-platform invariant to preserve here --
        // just internal self-consistency between this function, the pan code, and the pick ray.
        glm::vec3 OrbitForward(float pitchDegrees, float yawDegrees) {
            float pitch = glm::radians(pitchDegrees);
            float yaw = glm::radians(yawDegrees);
            return glm::normalize(glm::vec3{
                -std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                -std::cos(pitch) * std::cos(yaw)
            });
        }

        // Builds a quaternion matching the SAME composition order OrbitForward/ComposeWorldMtx
        // use (M = T*Rz*Ry*Rx) -- used only to seed SceneViewCamera3D::Rotation from a
        // CameraComponent's plain Euler Rotation (roll is always 0 for that seed, so this
        // reduces to qy*qx, exactly reproducing OrbitForward(pitch, yaw)).
        glm::quat EulerDegreesToQuat(const glm::vec3& rotationDegrees) {
            glm::quat qx = glm::angleAxis(glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            glm::quat qy = glm::angleAxis(glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat qz = glm::angleAxis(glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            return glm::normalize(qz * qy * qx);
        }

        // Inverse of EulerDegreesToQuat -- classic ZYX Tait-Bryan extraction, needed only to
        // feed SceneViewCamera3D::Rotation into IRenderer3D::BeginScene's Euler-degrees
        // parameter (that API stays Euler-based since it's shared with real CameraComponent
        // rendering, authored via plain Euler fields in the Properties panel). Degenerates at
        // pitch near +-90 the same way any Euler extraction does (Rotation.x/y/z can jump
        // discontinuously right at a pole crossing even though the camera's actual orientation,
        // and therefore the rendered view, stays perfectly smooth) -- this is exactly why the
        // orbit camera itself is stored as a quaternion and never round-tripped through Euler
        // except at this one renderer-API boundary.
        glm::vec3 QuatToEulerDegrees(const glm::quat& q) {
            glm::mat3 r = glm::mat3_cast(q);
            float y = std::asin(std::clamp(-r[0][2], -1.0f, 1.0f));
            float x = std::atan2(r[1][2], r[2][2]);
            float z = std::atan2(r[0][1], r[0][0]);
            return glm::degrees(glm::vec3(x, y, z));
        }

        // Bounding-sphere radius (before Transform scale) of what an entity's Mesh Renderer draws:
        // its Mesh Filter's mesh, shared with rendering bounds (MeshReference.h). A rough
        // bounding-sphere ray test is all the 3D picking does.
        float MeshBoundingRadius(Entity entity) {
            const MeshFilterComponent* filter = entity.TryGetComponent<MeshFilterComponent>();
            return MeshReference::BoundingRadius(filter ? filter->Mesh : AssetRef{});
        }

        float PointSegmentDistance(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
            ImVec2 ab{ b.x - a.x, b.y - a.y };
            float lenSq = ab.x * ab.x + ab.y * ab.y;
            float t = lenSq > 1e-6f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.0f, 1.0f) : 0.0f;
            ImVec2 closest{ a.x + ab.x * t, a.y + ab.y * t };
            return std::sqrt((p.x - closest.x) * (p.x - closest.x) + (p.y - closest.y) * (p.y - closest.y));
        }

        float Gizmo3DWorldAxisLength(const Projector3D& proj, const glm::vec3& originWorld) {
            const float depth = glm::dot(originWorld - proj.CameraPos, proj.Forward);
            if (depth <= 0.01f || proj.ViewportH <= 0)
                return 1.0f;
            const float worldPerPixel = (2.0f * depth * std::tan(glm::radians(proj.FovDegrees) * 0.5f)) /
                static_cast<float>(proj.ViewportH);
            return std::clamp(worldPerPixel * Gizmo3DTargetPixels, 0.01f, 1000.0f);
        }

        // Unity/Blender-style frustum wireframe for a camera entity, screen-space-projected via
        // `proj` -- shows at a glance where it's looking and its Fov/Zoom, not just its
        // position (the plain sphere marker alone). Visual only, not hit-tested (the sphere
        // marker still handles picking). `camForward/Right/Up` are the CAMERA ENTITY's own
        // basis (ignoring roll -- see caller's own comment), unrelated to the pane's own orbit
        // camera basis vectors of the same name.
        void DrawCameraFrustum(const Projector3D& proj, const glm::vec3& camPos, const glm::vec3& camForward, const glm::vec3& camRight, const glm::vec3& camUp, ProjectionType projectionType, float fovDegrees, float orthoHalfHeight, float aspect, float nearPlane, float farPlane, ImU32 color) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // FarPlane can be huge (hundreds/thousands of units) -- clamped to something that
            // reads clearly at typical scene scale instead of drawing lines off into nothing.
            float visDistance = std::min(farPlane, 300.0f);

            auto cornerAt = [&](float distance, float sx, float sy) {
                float halfH = (projectionType == ProjectionType::Perspective)
                    ? distance * std::tan(glm::radians(fovDegrees) * 0.5f)
                    : orthoHalfHeight;
                float halfW = halfH * aspect;
                return camPos + camForward * distance + camRight * (halfW * sx) + camUp * (halfH * sy);
            };

            glm::vec3 nearCorners[4] = { cornerAt(nearPlane, -1, -1), cornerAt(nearPlane, 1, -1), cornerAt(nearPlane, 1, 1), cornerAt(nearPlane, -1, 1) };
            glm::vec3 farCorners[4] = { cornerAt(visDistance, -1, -1), cornerAt(visDistance, 1, -1), cornerAt(visDistance, 1, 1), cornerAt(visDistance, -1, 1) };

            auto drawLine = [&](const glm::vec3& a, const glm::vec3& b) {
                ImVec2 sa, sb;
                if (proj.Project(a, sa) && proj.Project(b, sb))
                    drawList->AddLine(sa, sb, color, 1.5f);
            };

            for (int i = 0; i < 4; i++) {
                drawLine(nearCorners[i], nearCorners[(i + 1) % 4]);
                drawLine(farCorners[i], farCorners[(i + 1) % 4]);
                drawLine(nearCorners[i], farCorners[i]);
            }
            // Perspective only -- an orthographic frustum is a parallel box with no real apex
            // to converge to (would misleadingly look like a pyramid otherwise).
            if (projectionType == ProjectionType::Perspective) {
                for (int i = 0; i < 4; i++)
                    drawLine(camPos, nearCorners[i]);
            }
        }

        // A compact camera body + lens makes the camera entity itself readable even when its
        // frustum is edge-on or extends beyond the Scene pane. The forward arrow uses the same
        // basis as DrawCameraFrustum, so the icon never contradicts the Game camera direction.
        void DrawCameraBodyGizmo(const Projector3D& proj, const glm::vec3& cameraPosition, const glm::vec3& cameraForward, ImU32 color) {
            ImVec2 center;
            if (!proj.Project(cameraPosition, center))
                return;

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 bodyFill = (color & 0x00FFFFFFu) | 0xB0000000u;
            drawList->AddRectFilled(ImVec2(center.x - 8.0f, center.y - 5.0f), ImVec2(center.x + 8.0f, center.y + 5.0f), bodyFill, 2.0f);
            drawList->AddRect(ImVec2(center.x - 8.0f, center.y - 5.0f), ImVec2(center.x + 8.0f, center.y + 5.0f), color, 2.0f, 0, 1.5f);
            drawList->AddCircle(center, 3.0f, color, 12, 1.5f);

            ImVec2 tip;
            const float arrowLength = Gizmo3DWorldAxisLength(proj, cameraPosition) * 0.7f;
            if (!proj.Project(cameraPosition + cameraForward * arrowLength, tip))
                return;
            drawList->AddLine(center, tip, color, 1.75f);
            const ImVec2 direction{ tip.x - center.x, tip.y - center.y };
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            if (length <= 0.01f)
                return;
            const ImVec2 unit{ direction.x / length, direction.y / length };
            const ImVec2 perpendicular{ -unit.y, unit.x };
            drawList->AddTriangleFilled(tip,
                ImVec2(tip.x - unit.x * 7.0f + perpendicular.x * 3.5f, tip.y - unit.y * 7.0f + perpendicular.y * 3.5f),
                ImVec2(tip.x - unit.x * 7.0f - perpendicular.x * 3.5f, tip.y - unit.y * 7.0f - perpendicular.y * 3.5f), color);
        }

        // Unity-like directional-light gizmo: a yellow sun at the entity position plus three
        // parallel, arrowed rays. Directional lights have no physical range, therefore the ray
        // length is only an editor visual aid and is kept screen-size stable via the projector.
        // Rays travel along entity local -Z, the same direction RenderView derives before it
        // converts it to the shader's point-to-light vector.
        void DrawDirectionalLightGizmo(const Projector3D& proj, const TransformComponent& transform) {
            constexpr ImU32 SunColor = IM_COL32(255, 220, 70, 255);
            constexpr ImU32 RayColor = IM_COL32(255, 235, 130, 235);
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            ImVec2 center;
            if (proj.Project(transform.Translation, center)) {
                drawList->AddCircleFilled(center, 5.0f, SunColor, 16);
                drawList->AddCircle(center, 8.0f, SunColor, 16, 1.5f);
                for (int i = 0; i < 8; ++i) {
                    const float angle = glm::two_pi<float>() * static_cast<float>(i) / 8.0f;
                    const ImVec2 rayStart{ center.x + std::cos(angle) * 10.0f, center.y + std::sin(angle) * 10.0f };
                    const ImVec2 rayEnd{ center.x + std::cos(angle) * 14.0f, center.y + std::sin(angle) * 14.0f };
                    drawList->AddLine(rayStart, rayEnd, SunColor, 1.5f);
                }
            }

            const glm::quat rotation = EulerDegreesToQuat(transform.Rotation);
            const glm::vec3 rayDirection = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            const float size = Gizmo3DWorldAxisLength(proj, transform.Translation);
            const glm::vec2 offsets[3] = { { -0.55f, 0.38f }, { 0.0f, 0.0f }, { 0.55f, -0.38f } };
            for (const glm::vec2& offset : offsets) {
                const glm::vec3 start = transform.Translation + (right * offset.x + up * offset.y) * size;
                const glm::vec3 end = start + rayDirection * (size * 1.7f);
                ImVec2 screenStart, screenEnd;
                if (!proj.Project(start, screenStart) || !proj.Project(end, screenEnd))
                    continue;
                drawList->AddLine(screenStart, screenEnd, RayColor, 1.75f);
                const ImVec2 direction{ screenEnd.x - screenStart.x, screenEnd.y - screenStart.y };
                const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                if (length <= 0.01f)
                    continue;
                const ImVec2 unit{ direction.x / length, direction.y / length };
                const ImVec2 perpendicular{ -unit.y, unit.x };
                drawList->AddTriangleFilled(screenEnd,
                    ImVec2(screenEnd.x - unit.x * 7.0f + perpendicular.x * 3.5f, screenEnd.y - unit.y * 7.0f + perpendicular.y * 3.5f),
                    ImVec2(screenEnd.x - unit.x * 7.0f - perpendicular.x * 3.5f, screenEnd.y - unit.y * 7.0f - perpendicular.y * 3.5f), RayColor);
            }
        }

        // Polyline through world points, skipping segments with an unprojectable endpoint.
        void DrawWorldPolyline(const Projector3D& proj, const std::vector<glm::vec3>& points, ImU32 color, float thickness, bool closed) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const size_t count = points.size();
            for (size_t i = 0; i + (closed ? 0 : 1) < count; ++i) {
                ImVec2 a, b;
                if (proj.Project(points[i], a) && proj.Project(points[(i + 1) % count], b))
                    drawList->AddLine(a, b, color, thickness);
            }
        }

        std::vector<glm::vec3> WorldCircle(const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB, float radius, int segments = 48) {
            std::vector<glm::vec3> points;
            points.reserve(static_cast<size_t>(segments));
            for (int i = 0; i < segments; ++i) {
                const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
                points.push_back(center + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius);
            }
            return points;
        }

        // Bulb icon for every active Point Light; its Range sphere (3 great circles) when selected.
        void DrawPointLightGizmo(const Projector3D& proj, const TransformComponent& transform, const PointLightComponent& light, bool selected) {
            const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(
                0.55f + 0.45f * std::min(light.Color.r, 1.0f), 0.55f + 0.45f * std::min(light.Color.g, 1.0f), 0.55f + 0.45f * std::min(light.Color.b, 1.0f), 1.0f));
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 center;
            if (proj.Project(transform.Translation, center)) {
                drawList->AddCircleFilled(center, 5.5f, color, 16);
                drawList->AddCircle(center, 9.0f, color, 16, 1.25f);
                drawList->AddLine(ImVec2(center.x - 3.0f, center.y + 9.0f), ImVec2(center.x + 3.0f, center.y + 9.0f), color, 2.0f);
            }
            if (!selected)
                return;
            const ImU32 rangeColor = IM_COL32(255, 230, 120, 170);
            DrawWorldPolyline(proj, WorldCircle(transform.Translation, { 1, 0, 0 }, { 0, 1, 0 }, light.Range), rangeColor, 1.25f, true);
            DrawWorldPolyline(proj, WorldCircle(transform.Translation, { 1, 0, 0 }, { 0, 0, 1 }, light.Range), rangeColor, 1.25f, true);
            DrawWorldPolyline(proj, WorldCircle(transform.Translation, { 0, 1, 0 }, { 0, 0, 1 }, light.Range), rangeColor, 1.25f, true);
        }

        // Spot icon + direction ray always; the full cone (outer and inner angle) when selected.
        void DrawSpotLightGizmo(const Projector3D& proj, const TransformComponent& transform, const SpotLightComponent& light, bool selected) {
            const glm::quat rotation = EulerDegreesToQuat(transform.Rotation);
            const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            const ImU32 color = IM_COL32(255, 220, 90, 255);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 apex;
            if (proj.Project(transform.Translation, apex)) {
                drawList->AddCircleFilled(apex, 5.0f, color, 12);
                drawList->AddCircle(apex, 8.0f, color, 12, 1.25f);
            }
            const float length = selected ? light.Range : std::min(light.Range, Gizmo3DWorldAxisLength(proj, transform.Translation) * 1.6f);
            const auto drawCone = [&](float angleDegrees, ImU32 coneColor, int rays) {
                const float radius = length * std::tan(glm::radians(std::clamp(angleDegrees, 1.0f, 179.0f) * 0.5f));
                const glm::vec3 baseCenter = transform.Translation + forward * length;
                const std::vector<glm::vec3> base = WorldCircle(baseCenter, right, up, radius, 40);
                DrawWorldPolyline(proj, base, coneColor, 1.25f, true);
                for (int i = 0; i < rays; ++i) {
                    const glm::vec3& rim = base[static_cast<size_t>(i * static_cast<int>(base.size()) / rays)];
                    DrawWorldPolyline(proj, { transform.Translation, rim }, coneColor, 1.25f, false);
                }
            };
            drawCone(light.SpotAngle, selected ? IM_COL32(255, 230, 120, 200) : IM_COL32(255, 230, 120, 110), 4);
            if (selected && light.InnerSpotAngle > 0.5f)
                drawCone(light.InnerSpotAngle, IM_COL32(255, 190, 80, 120), 0);
        }

        constexpr float Collider3DHandleHalfSize = 5.0f;
        // Same order-of-magnitude as the pan/scale-drag sensitivities above -- Distance-scaled
        // so a resize drag feels proportionate regardless of how far the orbit camera is
        // zoomed out.
        constexpr float Collider3DResizeDragSensitivity = 0.002f;
        const ImU32 Collider3DColor = IM_COL32(60, 230, 90, 255);

        // Wireframe box collider (12 edges), oriented by `rotation` (the entity's own world
        // rotation -- unlike the 2D pane's collider gizmos, a 3D BoxCollider's shape really
        // does rotate with its body, see Scene.cpp's Bullet body creation) and sized by
        // `halfExtents` (BoxCollider3DComponent::Size is already a half-extent). Same
        // proj.Project-per-point technique DrawCameraFrustum already established. Also draws
        // (and returns the screen position of) one resize handle per positive axis at that
        // face's center -- dragging one lets Size be edited directly in the viewport instead
        // of only through the Properties panel. A handle whose world position is behind the
        // camera reports NaN so the caller skips hit-testing/dragging it that frame, same
        // tolerance the translate/rotate gizmo already has for an off-screen handle.
        void DrawBoxCollider3D(const Projector3D& proj, const glm::vec3& center, const glm::quat& rotation, const glm::vec3& halfExtents, ImVec2 outHandles[3]) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            glm::vec3 axisX = rotation * glm::vec3(halfExtents.x, 0.0f, 0.0f);
            glm::vec3 axisY = rotation * glm::vec3(0.0f, halfExtents.y, 0.0f);
            glm::vec3 axisZ = rotation * glm::vec3(0.0f, 0.0f, halfExtents.z);

            // Index = (sx?4:0) + (sy?2:0) + (sz?1:0), sx/sy/sz each -1 or +1.
            glm::vec3 corners[8];
            int index = 0;
            for (float sx : { -1.0f, 1.0f })
                for (float sy : { -1.0f, 1.0f })
                    for (float sz : { -1.0f, 1.0f })
                        corners[index++] = center + axisX * sx + axisY * sy + axisZ * sz;

            auto drawEdge = [&](int a, int b) {
                ImVec2 sa, sb;
                if (proj.Project(corners[a], sa) && proj.Project(corners[b], sb))
                    drawList->AddLine(sa, sb, Collider3DColor, 2.0f);
            };
            drawEdge(0, 1); drawEdge(2, 3); drawEdge(4, 5); drawEdge(6, 7); // edges along Z
            drawEdge(0, 2); drawEdge(1, 3); drawEdge(4, 6); drawEdge(5, 7); // edges along Y
            drawEdge(0, 4); drawEdge(1, 5); drawEdge(2, 6); drawEdge(3, 7); // edges along X

            glm::vec3 faceHandles[3] = { center + axisX, center + axisY, center + axisZ };
            for (int i = 0; i < 3; i++) {
                ImVec2 screen;
                if (proj.Project(faceHandles[i], screen)) {
                    drawList->AddRectFilled(ImVec2(screen.x - Collider3DHandleHalfSize, screen.y - Collider3DHandleHalfSize),
                        ImVec2(screen.x + Collider3DHandleHalfSize, screen.y + Collider3DHandleHalfSize), Collider3DColor);
                    outHandles[i] = screen;
                } else {
                    outHandles[i] = ImVec2(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
                }
            }
        }

        // Wireframe sphere collider -- three orthogonal great circles (same per-point
        // proj.Project technique as DrawAndHitTestGizmo3D's own rotation rings below), plus one
        // resize handle on the +X equator point (dragging it edits Radius directly).
        void DrawSphereCollider3D(const Projector3D& proj, const glm::vec3& center, float radius, ImVec2& outHandle) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            constexpr int Segments = 32;
            const glm::vec3 basisPairs[3][2] = {
                { { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // circle around X
                { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, // circle around Y
                { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // circle around Z
            };
            for (auto& pair : basisPairs) {
                std::vector<ImVec2> points;
                points.reserve(Segments + 1);
                for (int i = 0; i <= Segments; i++) {
                    float t = (static_cast<float>(i) / Segments) * 2.0f * 3.14159265f;
                    glm::vec3 worldPt = center + (pair[0] * std::cos(t) + pair[1] * std::sin(t)) * radius;
                    ImVec2 screenPt;
                    if (proj.Project(worldPt, screenPt))
                        points.push_back(screenPt);
                }
                if (points.size() >= 2)
                    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), Collider3DColor, ImDrawFlags_None, 2.0f);
            }

            ImVec2 screen;
            if (proj.Project(center + glm::vec3(radius, 0.0f, 0.0f), screen)) {
                drawList->AddCircleFilled(screen, Collider3DHandleHalfSize, Collider3DColor);
                outHandle = screen;
            } else {
                outHandle = ImVec2(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
            }
        }

        // Drives one resize-handle drag via ImGui's own per-widget active/dragging state
        // (InvisibleButton), deliberately NOT threaded through EditorContext's
        // DraggingGizmoAxis/-Screen the way the Translate/Rotate/Scale gizmo is -- that
        // machinery exists specifically to keep a drag tracking correctly across BOTH
        // Top/Bottom panes sharing one selection, which a collider handle (only ever drawn in
        // the pane matching the collider's own effective screen, see ShouldRenderOnScreen)
        // never needs. `centerScreen`/`handleScreen` are NaN-checked by the caller before this
        // is invoked. Returns the new value for the dragged axis/radius, or the unchanged
        // `current` if not being dragged this frame.
        float DragCollider3DHandle(const char* id, ImVec2 centerScreen, ImVec2 handleScreen, float cameraDistance, float current) {
            ImGui::SetCursorScreenPos(ImVec2(handleScreen.x - Collider3DHandleHalfSize, handleScreen.y - Collider3DHandleHalfSize));
            ImGui::InvisibleButton(id, ImVec2(Collider3DHandleHalfSize * 2.0f, Collider3DHandleHalfSize * 2.0f));
            if (!ImGui::IsItemActive() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                return current;

            // Screen-space direction the axis actually points in (not assumed to be purely
            // horizontal/vertical -- the orbit camera can be at any angle) -- dragging the mouse
            // further along that same screen direction increases the value, same "outward =
            // bigger" convention the Scale gizmo's handles already use.
            glm::vec2 screenDir{ handleScreen.x - centerScreen.x, handleScreen.y - centerScreen.y };
            float screenDirLen = glm::length(screenDir);
            if (screenDirLen < 1e-3f)
                return current; // handle projected right on top of center -- no reliable direction this frame
            screenDir /= screenDirLen;

            ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
            float dragAmount = (mouseDelta.x * screenDir.x + mouseDelta.y * screenDir.y) * cameraDistance * Collider3DResizeDragSensitivity;
            return std::max(0.01f, current + dragAmount);
        }


        // 2D counterpart of DragCollider3DHandle above -- same InvisibleButton-driven drag,
        // same outward-along-the-handle-direction projection, but no cameraDistance-scaled
        // sensitivity constant needed: the 2D pane's own camera.Zoom already IS the exact
        // pixels-per-world-unit conversion factor (see DrawScenePane2D's worldToPaneScreen),
        // so dividing by it converts a screen-space mouse delta straight into world units.
        float DragCollider2DHandle(const char* id, ImVec2 centerScreen, ImVec2 handleScreen, float zoom, float current) {
            ImGui::SetCursorScreenPos(ImVec2(handleScreen.x - Collider2DHandleHalfSize, handleScreen.y - Collider2DHandleHalfSize));
            ImGui::InvisibleButton(id, ImVec2(Collider2DHandleHalfSize * 2.0f, Collider2DHandleHalfSize * 2.0f));
            if (!ImGui::IsItemActive() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                return current;

            glm::vec2 screenDir{ handleScreen.x - centerScreen.x, handleScreen.y - centerScreen.y };
            float screenDirLen = glm::length(screenDir);
            if (screenDirLen < 1e-3f)
                return current; // handle sits right on top of center -- no reliable direction this frame
            screenDir /= screenDirLen;

            ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
            float dragAmount = (mouseDelta.x * screenDir.x + mouseDelta.y * screenDir.y) / zoom;
            return std::max(0.01f, current + dragAmount);
        }

        glm::vec2 GetPolygonVertex2DConst(const PolygonCollider2DComponent& poly, int index) {
            switch (index) {
                case 0: return poly.Vertex0;
                case 1: return poly.Vertex1;
                case 2: return poly.Vertex2;
                case 3: return poly.Vertex3;
                case 4: return poly.Vertex4;
                case 5: return poly.Vertex5;
                case 6: return poly.Vertex6;
                default: return poly.Vertex7;
            }
        }

        bool PointInPolygon2D(const glm::vec2& point, const PolygonCollider2DComponent& poly) {
            bool inside = false;
            int vertexCount = std::clamp(poly.VertexCount, 3, 8);
            for (int i = 0, j = vertexCount - 1; i < vertexCount; j = i++) {
                glm::vec2 vi = GetPolygonVertex2DConst(poly, i);
                glm::vec2 vj = GetPolygonVertex2DConst(poly, j);
                if (((vi.y > point.y) != (vj.y > point.y)) &&
                    (point.x < (vj.x - vi.x) * (point.y - vi.y) / (vj.y - vi.y + 1e-8f) + vi.x))
                    inside = !inside;
            }
            return inside;
        }

        bool PointInCapsule2D(const glm::vec2& point, const glm::vec2& center, float radius, float height) {
            float halfBody = std::max(0.0f, (height - 2.0f * radius) * 0.5f);
            glm::vec2 topCap = center + glm::vec2{ 0.0f, halfBody };
            glm::vec2 bottomCap = center - glm::vec2{ 0.0f, halfBody };
            if (glm::distance(point, topCap) <= radius || glm::distance(point, bottomCap) <= radius)
                return true;
            return std::abs(point.x - center.x) <= radius &&
                   point.y >= bottomCap.y && point.y <= topCap.y;
        }

        void DrawCapsule2DOutline(ImDrawList* drawList, const ImVec2& topCapScreen, const ImVec2& bottomCapScreen,
            float radiusPx, ImU32 color) {
            drawList->AddLine(ImVec2(topCapScreen.x + radiusPx, topCapScreen.y),
                ImVec2(bottomCapScreen.x + radiusPx, bottomCapScreen.y), color, 2.0f);
            drawList->AddLine(ImVec2(topCapScreen.x - radiusPx, topCapScreen.y),
                ImVec2(bottomCapScreen.x - radiusPx, bottomCapScreen.y), color, 2.0f);
            constexpr int arcSegments = 16;
            drawList->PathClear();
            drawList->PathArcTo(topCapScreen, radiusPx, 0.0f, glm::pi<float>(), arcSegments);
            drawList->PathStroke(color, 0, 2.0f);
            drawList->PathClear();
            drawList->PathArcTo(bottomCapScreen, radiusPx, glm::pi<float>(), glm::pi<float>() * 2.0f, arcSegments);
            drawList->PathStroke(color, 0, 2.0f);
        }

        void DrawCapsuleCollider3D(const Projector3D& proj, const glm::vec3& center, const glm::quat& rotation,
            float radius, float height, ImVec2& outRadiusHandle, ImVec2& outHeightHandle) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float halfBody = std::max(0.0f, (height - 2.0f * radius) * 0.5f);
            glm::mat3 rotMat = glm::mat3(rotation);
            auto toWorld = [&](const glm::vec3& local) { return center + rotMat * local; };

            const int circleSegments = 24;
            auto drawCircle = [&](const glm::vec3& circleCenter, const glm::vec3& axisA, const glm::vec3& axisB) {
                std::vector<ImVec2> points;
                points.reserve(circleSegments + 1);
                for (int i = 0; i <= circleSegments; i++) {
                    float angle = (static_cast<float>(i) / circleSegments) * glm::pi<float>() * 2.0f;
                    glm::vec3 point = circleCenter + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius;
                    ImVec2 screen;
                    if (proj.Project(point, screen))
                        points.push_back(screen);
                }
                if (points.size() >= 2)
                    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), Collider3DColor, ImDrawFlags_None, 2.0f);
            };

            glm::vec3 axisY = rotMat * glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 axisX = rotMat * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 axisZ = rotMat * glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 topCap = toWorld({ 0.0f, halfBody, 0.0f });
            glm::vec3 bottomCap = toWorld({ 0.0f, -halfBody, 0.0f });
            drawCircle(topCap, axisX, axisZ);
            drawCircle(bottomCap, axisX, axisZ);
            drawCircle(center, axisX, axisZ);

            for (int i = 0; i < 4; i++) {
                float angle = static_cast<float>(i) * glm::pi<float>() * 0.5f;
                glm::vec3 offset = (axisX * std::cos(angle) + axisZ * std::sin(angle)) * radius;
                ImVec2 topScreen, bottomScreen;
                if (proj.Project(topCap + offset, topScreen) && proj.Project(bottomCap + offset, bottomScreen))
                    drawList->AddLine(topScreen, bottomScreen, Collider3DColor, 2.0f);
            }

            auto radiusAtLocalY = [halfBody, radius](float yLocal) {
                if (yLocal > halfBody) {
                    float d = yLocal - halfBody;
                    return std::sqrt(std::max(radius * radius - d * d, 0.0f));
                }
                if (yLocal < -halfBody) {
                    float d = -halfBody - yLocal;
                    return std::sqrt(std::max(radius * radius - d * d, 0.0f));
                }
                return radius;
            };
            auto drawMeridian = [&](const glm::vec3& sideAxis, float sideSign) {
                std::vector<ImVec2> points;
                points.reserve(17);
                constexpr int segments = 16;
                for (int i = 0; i <= segments; i++) {
                    float t = static_cast<float>(i) / segments;
                    float yLocal = (-halfBody - radius) + t * (2.0f * halfBody + 2.0f * radius);
                    float rAtY = radiusAtLocalY(yLocal);
                    glm::vec3 local = axisY * yLocal + sideAxis * (rAtY * sideSign);
                    ImVec2 screen;
                    if (proj.Project(toWorld(local), screen))
                        points.push_back(screen);
                }
                if (points.size() >= 2)
                    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), Collider3DColor, ImDrawFlags_None, 2.0f);
            };
            drawMeridian(axisX, 1.0f);
            drawMeridian(axisX, -1.0f);
            drawMeridian(axisZ, 1.0f);
            drawMeridian(axisZ, -1.0f);

            glm::vec3 radiusPoint = toWorld({ radius, 0.0f, 0.0f });
            glm::vec3 heightPoint = toWorld({ 0.0f, halfBody + radius, 0.0f });
            ImVec2 radiusScreen, heightScreen;
            if (proj.Project(radiusPoint, radiusScreen)) {
                drawList->AddCircleFilled(radiusScreen, Collider3DHandleHalfSize, Collider3DColor);
                outRadiusHandle = radiusScreen;
            } else {
                outRadiusHandle = ImVec2(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
            }
            if (proj.Project(heightPoint, heightScreen)) {
                drawList->AddCircleFilled(heightScreen, Collider3DHandleHalfSize, Collider3DColor);
                outHeightHandle = heightScreen;
            } else {
                outHeightHandle = ImVec2(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
            }
        }

        // Clips the segment a-b against a plane `nearDistance` in front of the camera (NOT
        // Projector3D::Project's own bare 0.01 "still technically in front" threshold -- see
        // below for why) and returns the portion actually beyond it. Necessary because
        // Projector3D::Project has no clipping of its own -- it either projects a point or
        // reports failure -- so a LINE (as opposed to the single points every other gizmo in
        // this pane projects) needs this extra step: without it, a grid line long enough for
        // one endpoint to dip behind the camera would vanish ENTIRELY (both-endpoints-must-
        // project) rather than just being clipped at the visible edge. Returns false if the
        // whole segment is behind `nearDistance`.
        //
        // `nearDistance` is passed in by the caller (DrawGrid3D uses a chunk of a grid cell,
        // not a bare epsilon) deliberately larger than strictly necessary: a point clipped
        // right at Project's own 0.01 threshold can still have a viewZ small enough that
        // Project's perspective divide blows its screen coordinates up to an enormous
        // magnitude (a grid line running nearly parallel to the view direction, e.g. close to
        // the horizon, is exactly the case that triggers this) -- ImGui/the GPU rasterizing
        // near-infinite line endpoints is what actually produced the reported "grid lines
        // flicker/vanish while zooming or panning," not the clipping itself being wrong.
        bool ClipSegmentToCameraFront(const Projector3D& proj, const glm::vec3& a, const glm::vec3& b, float nearDistance, glm::vec3& outA, glm::vec3& outB) {
            float za = glm::dot(a - proj.CameraPos, proj.Forward);
            float zb = glm::dot(b - proj.CameraPos, proj.Forward);
            bool aFront = za > nearDistance, bFront = zb > nearDistance;
            if (!aFront && !bFront)
                return false;
            if (aFront && bFront) {
                outA = a; outB = b;
                return true;
            }
            float t = (nearDistance - za) / (zb - za); // za/zb straddle nearDistance here, denominator is safely non-zero
            glm::vec3 clipPoint = a + (b - a) * t;
            if (aFront) { outA = a; outB = clipPoint; }
            else { outA = clipPoint; outB = b; }
            return true;
        }

        // Projects a world-space grid line first, then clips the resulting 2D segment to the
        // Scene image itself. At a shallow/diagonal view, valid grid lines often have endpoints
        // far off-screen while still crossing the viewport. The old endpoint rejection dropped
        // those lines wholesale, causing the grid to disappear in large pieces while orbiting.
        bool ClipLineToViewport(const Projector3D& proj, ImVec2& a, ImVec2& b) {
            const float left = proj.ImagePos.x;
            const float right = left + proj.ViewportW;
            const float top = proj.ImagePos.y;
            const float bottom = top + proj.ViewportH;
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            float first = 0.0f, last = 1.0f;
            const auto clip = [&first, &last](float p, float q) {
                if (std::abs(p) < 1e-6f)
                    return q >= 0.0f;
                const float t = q / p;
                if (p < 0.0f) {
                    if (t > last) return false;
                    if (t > first) first = t;
                } else {
                    if (t < first) return false;
                    if (t < last) last = t;
                }
                return true;
            };
            if (!clip(-dx, a.x - left) || !clip(dx, right - a.x) ||
                !clip(-dy, a.y - top) || !clip(dy, bottom - a.y))
                return false;
            const ImVec2 original = a;
            a = ImVec2(original.x + first * dx, original.y + first * dy);
            b = ImVec2(original.x + last * dx, original.y + last * dy);
            return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(b.x) && std::isfinite(b.y);
        }

        // Unity/Cocos-style ground-plane grid (XZ plane at Y=0) for the 3D Scene pane -- same
        // proj.Project-per-line-segment overlay technique DrawCameraFrustum/DrawBoxCollider3D
        // already use, so it shares their same known limitation (a flat screen-space overlay,
        // not real depth-tested geometry -- a grid line "behind" a mesh from the camera's
        // viewpoint still draws on top of it, same as every other gizmo in this pane). The
        // patch of grid lines is re-centered near `cameraTarget` (snapped to the nearest cell)
        // every frame so panning always shows a grid "under" wherever you're looking rather
        // than a fixed world-origin-centered patch that scrolls out of view entirely -- but the
        // X/Z axis lines themselves stay at their true world position (0 on the other axis)
        // regardless of camera position, just extended far enough to stay visible while panned.
        // `extent` (half-width of the patch) is scaled to the orbit camera's own Distance by
        // the caller, rather than one fixed size -- a fixed large extent regardless of zoom
        // meant most grid lines were far longer than the visible area at typical zoom, making
        // them far more likely to need clipping (or, before ClipSegmentToCameraFront existed,
        // to vanish outright) as the camera moved.
        // XZ grid of +-extent around `origin` (already snapped to the cell size by the caller).
        void DrawGrid3D(const Projector3D& proj, const glm::vec3& origin, float cellSize, float extent) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            int halfLineCount = std::max(1, static_cast<int>(extent / cellSize));
            float centerX = std::round(origin.x / cellSize) * cellSize;
            float centerZ = std::round(origin.z / cellSize) * cellSize;
            // The 2D viewport clip below makes a small near-plane clip safe. A cell-sized
            // clip removed nearby grid lines at diagonal angles.
            constexpr float nearClipDistance = 0.05f;

            auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) {
                glm::vec3 clippedA, clippedB;
                if (!ClipSegmentToCameraFront(proj, a, b, nearClipDistance, clippedA, clippedB))
                    return;
                ImVec2 sa, sb;
                if (!proj.Project(clippedA, sa) || !proj.Project(clippedB, sb))
                    return;
                if (!ClipLineToViewport(proj, sa, sb))
                    return;
                drawList->AddLine(sa, sb, color, thickness);
            };

            drawList->PushClipRect(proj.ImagePos,
                ImVec2(proj.ImagePos.x + proj.ViewportW, proj.ImagePos.y + proj.ViewportH), true);
            const ImU32 gridColor = IM_COL32(120, 120, 130, 90);
            for (int i = -halfLineCount; i <= halfLineCount; i++) {
                float x = centerX + i * cellSize;
                line({ x, 0.0f, centerZ - extent }, { x, 0.0f, centerZ + extent }, gridColor, 1.0f);
            }
            for (int i = -halfLineCount; i <= halfLineCount; i++) {
                float z = centerZ + i * cellSize;
                line({ centerX - extent, 0.0f, z }, { centerX + extent, 0.0f, z }, gridColor, 1.0f);
            }

            // World axis lines, Unity's own X=red/Z=blue convention (matches the 2D pane's grid
            // and the orientation gizmo below) -- drawn last so they're on top of the plain grid.
            line({ centerX - extent, 0.0f, 0.0f }, { centerX + extent, 0.0f, 0.0f }, IM_COL32(230, 70, 70, 255), 2.0f);
            line({ 0.0f, 0.0f, centerZ - extent }, { 0.0f, 0.0f, centerZ + extent }, IM_COL32(80, 140, 230, 255), 2.0f);
            drawList->PopClipRect();
        }

}
