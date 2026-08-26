#include "DualityEditor/Panels/ScenePanel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneGizmo.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    namespace {
        // Dragging the Scale gizmo's axis handles moves the mouse by tens
        // to hundreds of world units (same units as Translation, i.e.
        // sprite sizes of ~16-360), but Scale is a unitless multiplier
        // starting at 1.0 -- this converts one to the other so a drag
        // feels proportionate instead of scale exploding after a few
        // pixels of movement.
        constexpr float ScaleDragSensitivity = 0.01f;

        // A handful of "nice" round grid-cell sizes stepped by zoom level, rather than one
        // fixed size (which would look absurdly dense zoomed out or absurdly sparse zoomed in
        // across the 0.1x-5x zoom range) or fully continuous/smoothly-blended LOD (real Unity
        // behavior, but more machinery than this first pass needs).
        float GridCellSize2D(float zoom) {
            if (zoom > 2.5f) return 20.0f;
            if (zoom > 1.0f) return 50.0f;
            if (zoom > 0.4f) return 100.0f;
            return 250.0f;
        }
        float GridCellSize3D(float cameraDistance) {
            if (cameraDistance < 50.0f) return 10.0f;
            if (cameraDistance < 200.0f) return 25.0f;
            if (cameraDistance < 800.0f) return 50.0f;
            if (cameraDistance < 2000.0f) return 200.0f;
            return 500.0f;
        }

        // Converts a world-space delta (raw mouse movement) into the local-space
        // delta that produces the same world-space effect, given `entity`'s parent
        // chain -- un-rotates/un-scales by the parent's world rotation/scale. Used
        // by the Translate/Scale gizmo drag below (Rotate needs no conversion since
        // rotation composition is additive). Identity for a root entity, matching
        // every case before parenting existed.
        glm::vec2 WorldDeltaToLocal(Scene& scene, Entity entity, glm::vec2 worldDelta) {
            Entity parent = entity.GetComponent<HierarchyComponent>().Parent;
            if (!parent)
                return worldDelta;
            TransformComponent parentWorld = scene.GetWorldTransform(parent);
            float parentRad = glm::radians(parentWorld.Rotation.z);
            float cosR = std::cos(-parentRad), sinR = std::sin(-parentRad);
            glm::vec2 unrotated{ worldDelta.x * cosR - worldDelta.y * sinR, worldDelta.x * sinR + worldDelta.y * cosR };
            auto safeDiv = [](float a, float b) { return std::abs(b) > 1e-6f ? a / b : a; };
            return { safeDiv(unrotated.x, parentWorld.Scale.x), safeDiv(unrotated.y, parentWorld.Scale.y) };
        }

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

        // Bounding-sphere radius for a unit-sized primitive (see PrimitiveMeshes.cpp -- Cube
        // spans [-0.5,0.5]^3, Sphere has radius 0.5, Plane spans [-0.5,0.5] in XZ) before the
        // entity's own Scale is applied. A rough approximation (not exact per-axis OBB/AABB),
        // matching the plan's explicit "simple bounding-sphere ray test" scope cut -- no 3D
        // gizmo/precise picking yet.
        float PrimitiveBoundingRadius(MeshPrimitive primitive) {
            switch (primitive) {
                case MeshPrimitive::Sphere: return 0.5f;
                case MeshPrimitive::Plane:  return 0.5f * std::sqrt(2.0f);
                case MeshPrimitive::Cube:
                default:                    return 0.5f * std::sqrt(3.0f);
            }
        }

        // Same fallback convention as rendering (see SceneRenderer.cpp's ResolveMeshGeometry):
        // an imported Mesh, when it resolves, replaces the procedural Primitive's own radius.
        float MeshBoundingRadius(const MeshRendererComponent& mesh) {
            if (!mesh.Mesh.Guid.empty()) {
                std::string path = AssetDatabase::ResolvePath(mesh.Mesh.Guid);
                if (!path.empty()) {
                    float radius = MeshLoader::Load(path).BoundingRadius;
                    if (radius > 0.0f)
                        return radius;
                }
            }
            return PrimitiveBoundingRadius(mesh.Primitive);
        }

        // Projects world-space points into this pane's screen-space pixels, given the orbit
        // camera's own basis vectors -- the exact inverse of the pick-ray math in
        // DrawScenePane3D's click handler (same forward/right/up, fov, aspect). Shared by the
        // 3D gizmo's drawing, hit-testing, and drag math below.
        struct Projector3D {
            glm::vec3 CameraPos, Forward, Right, Up;
            float FovDegrees, Aspect;
            ImVec2 ImagePos;
            int ViewportW, ViewportH;

            bool Project(const glm::vec3& worldPos, ImVec2& outScreen) const {
                glm::vec3 rel = worldPos - CameraPos;
                // Forward-facing depth: positive and growing for a point further in front of
                // the camera (rel increasingly parallel to Forward) -- NOT negated, matching
                // the pick-ray code's own dot(toSphere, rayDir) > 0 "in front" convention below.
                float viewZ = glm::dot(rel, Forward);
                if (viewZ <= 0.01f)
                    return false; // behind the camera
                float tanHalfFov = std::tan(glm::radians(FovDegrees) * 0.5f);
                float ndcX = glm::dot(rel, Right) / (viewZ * tanHalfFov * Aspect);
                float ndcY = glm::dot(rel, Up) / (viewZ * tanHalfFov);
                outScreen = ImVec2(ImagePos.x + (ndcX * 0.5f + 0.5f) * ViewportW, ImagePos.y + (0.5f - ndcY * 0.5f) * ViewportH);
                return true;
            }
        };

        float PointSegmentDistance(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
            ImVec2 ab{ b.x - a.x, b.y - a.y };
            float lenSq = ab.x * ab.x + ab.y * ab.y;
            float t = lenSq > 1e-6f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lenSq, 0.0f, 1.0f) : 0.0f;
            ImVec2 closest{ a.x + ab.x * t, a.y + ab.y * t };
            return std::sqrt((p.x - closest.x) * (p.x - closest.x) + (p.y - closest.y) * (p.y - closest.y));
        }

        constexpr float Gizmo3DAxisLength = 60.0f;
        constexpr float Gizmo3DHitBand = 8.0f;
        constexpr float Gizmo3DScaleHandleHalfSize = 5.0f;
        constexpr float Gizmo3DCenterRadius = 6.0f;

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

        constexpr float Collider2DHandleHalfSize = 5.0f;

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

        // Second line of defense against the same "near-horizon line blows up to an extreme
        // screen coordinate" problem ClipSegmentToCameraFront's larger near-distance already
        // reduces -- even a comfortably-in-front point can still project absurdly far outside
        // the viewport for a line nearly edge-on to the camera. Rejecting those outright (they
        // contribute nothing visible anyway) avoids handing ImGui/the GPU rasterizer
        // coordinates large enough to risk float-precision artifacts.
        bool IsReasonableScreenPoint(const Projector3D& proj, const ImVec2& p) {
            float margin = std::max(proj.ViewportW, proj.ViewportH) * 4.0f;
            return p.x > proj.ImagePos.x - margin && p.x < proj.ImagePos.x + proj.ViewportW + margin &&
                   p.y > proj.ImagePos.y - margin && p.y < proj.ImagePos.y + proj.ViewportH + margin;
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
        void DrawGrid3D(const Projector3D& proj, const glm::vec3& cameraTarget, float cellSize, float extent) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            int halfLineCount = std::max(1, static_cast<int>(extent / cellSize));
            float centerX = std::round(cameraTarget.x / cellSize) * cellSize;
            float centerZ = std::round(cameraTarget.z / cellSize) * cellSize;
            // See ClipSegmentToCameraFront's own comment -- a meaningful fraction of a grid
            // cell, not a bare "still technically in front" epsilon.
            float nearClipDistance = std::max(cellSize * 0.5f, 1.0f);

            auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) {
                glm::vec3 clippedA, clippedB;
                if (!ClipSegmentToCameraFront(proj, a, b, nearClipDistance, clippedA, clippedB))
                    return;
                ImVec2 sa, sb;
                if (!proj.Project(clippedA, sa) || !proj.Project(clippedB, sb))
                    return;
                if (!IsReasonableScreenPoint(proj, sa) || !IsReasonableScreenPoint(proj, sb))
                    return;
                drawList->AddLine(sa, sb, color, thickness);
            };

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
        }

        constexpr float OrientationGizmoRadius = 40.0f;
        constexpr float OrientationGizmoMargin = 55.0f;
        constexpr float OrientationGizmoAxisLength = 30.0f;
        constexpr float OrientationGizmoTipRadius = 8.0f;

        // Blender/Unity-style ViewCube-equivalent, fixed in the pane's own top-right corner
        // (screen-space, NOT tied to any world position) -- shows the world X/Y/Z axes relative
        // to the CURRENT camera orientation, and doubles as a click target: clicking a filled
        // tip snaps the orbit camera to look straight down that axis (keeping the same Target/
        // Distance, only Rotation changes), the same "operate the view" convenience Unity's own
        // ViewCube/axis gizmo provides. Returns true if this frame's click was consumed by the
        // gizmo (hit-tested BEFORE the pane's own entity-pick-ray, so clicking it never also
        // tries to select/deselect whatever's underneath).
        bool DrawAndInteractOrientationGizmo3D(SceneViewCamera3D& camera3D, ImVec2 imagePos, int viewportW, bool imageHovered) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 center(imagePos.x + viewportW - OrientationGizmoMargin, imagePos.y + OrientationGizmoMargin);
            drawList->AddCircleFilled(center, OrientationGizmoRadius, IM_COL32(40, 40, 45, 160));

            struct AxisInfo { glm::vec3 Dir; ImU32 Color; const char* Label; float PosPitch, PosYaw; };
            // Pitch/Yaw here is the EulerDegreesToQuat pair that makes the camera look straight
            // down the NEGATIVE of this axis (i.e. positioned on the positive side, looking back
            // toward the origin) -- matches OrbitForward(pitch,yaw)'s own (-sin,sin,-cos) shape,
            // derived once by hand for each of the 3 axes.
            AxisInfo axes[3] = {
                { { 1.0f, 0.0f, 0.0f }, IM_COL32(230, 70, 70, 255), "X", 0.0f, 90.0f },
                { { 0.0f, 1.0f, 0.0f }, IM_COL32(80, 200, 100, 255), "Y", -90.0f, 0.0f },
                { { 0.0f, 0.0f, 1.0f }, IM_COL32(80, 140, 230, 255), "Z", 0.0f, 0.0f },
            };

            glm::quat invRot = glm::inverse(camera3D.Rotation);
            struct Drawn { ImVec2 tip; ImU32 color; const char* label; float depth; bool positive; float pitch, yaw; };
            std::vector<Drawn> drawn;
            drawn.reserve(6);
            for (auto& info : axes) {
                for (float sign : { 1.0f, -1.0f }) {
                    // View-space direction of this (possibly negated) world axis -- x/y become
                    // the 2D screen offset directly (view space is already camera-relative),
                    // z is used only to depth-sort so a near tip draws over a far one.
                    glm::vec3 viewDir = invRot * (info.Dir * sign);
                    ImVec2 tip(center.x + viewDir.x * OrientationGizmoAxisLength, center.y - viewDir.y * OrientationGizmoAxisLength);
                    bool positive = sign > 0.0f;
                    // The negative-direction tip snaps to the SAME look-down-this-axis target as
                    // its positive counterpart's OPPOSITE face -- i.e. clicking "-X" looks from
                    // -X back toward the origin, the mirror of "+X"'s pitch/yaw.
                    float pitch = positive ? info.PosPitch : -info.PosPitch;
                    float yaw = positive ? info.PosYaw : (info.PosYaw + 180.0f);
                    drawn.push_back({ tip, info.Color, info.Label, viewDir.z, positive, pitch, yaw });
                }
            }
            std::sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) { return a.depth < b.depth; });

            ImVec2 mouse = ImGui::GetIO().MousePos;
            int hoveredIndex = -1;
            for (size_t i = 0; i < drawn.size(); i++) {
                float dx = mouse.x - drawn[i].tip.x, dy = mouse.y - drawn[i].tip.y;
                if (std::sqrt(dx * dx + dy * dy) <= OrientationGizmoTipRadius)
                    hoveredIndex = static_cast<int>(i);
            }

            for (size_t i = 0; i < drawn.size(); i++) {
                const Drawn& d = drawn[i];
                bool isHovered = (static_cast<int>(i) == hoveredIndex);
                drawList->AddLine(center, d.tip, d.color, 2.0f);
                if (d.positive) {
                    drawList->AddCircleFilled(d.tip, isHovered ? OrientationGizmoTipRadius + 2.0f : OrientationGizmoTipRadius, d.color);
                    ImVec2 textSize = ImGui::CalcTextSize(d.label);
                    drawList->AddText(ImVec2(d.tip.x - textSize.x * 0.5f, d.tip.y - textSize.y * 0.5f), IM_COL32(20, 20, 20, 255), d.label);
                } else {
                    drawList->AddCircleFilled(d.tip, isHovered ? OrientationGizmoTipRadius - 1.0f : OrientationGizmoTipRadius - 2.0f, IM_COL32(60, 60, 65, 255));
                    drawList->AddCircle(d.tip, OrientationGizmoTipRadius - 2.0f, d.color, 12, 1.5f);
                }
            }

            if (imageHovered && hoveredIndex >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const Drawn& hit = drawn[hoveredIndex];
                camera3D.Rotation = EulerDegreesToQuat({ hit.pitch, hit.yaw, 0.0f });
                return true;
            }
            // Swallow a click anywhere within the gizmo's own circular backdrop even if it
            // missed every tip -- otherwise a near-miss click would fall through to the pane's
            // entity-pick-ray underneath the gizmo's backdrop.
            if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                float dx = mouse.x - center.x, dy = mouse.y - center.y;
                if (std::sqrt(dx * dx + dy * dy) <= OrientationGizmoRadius)
                    return true;
            }
            return false;
        }

        // 3D analog of DrawAndHitTestGizmo2D (SceneGizmo.cpp) -- lives here rather than there
        // since it needs this pane's own camera projection (Projector3D), which the 2D gizmo
        // (already given pre-computed screen-space coordinates by its caller) has no use for.
        // Draws X/Y/Z axis handles (Translate/Scale) or three rotation rings (Rotate) projected
        // from `originWorld`, and returns whichever handle the mouse is over -- same contract as
        // the 2D version, this never touches a TransformComponent itself.
        GizmoAxis DrawAndHitTestGizmo3D(GizmoMode mode, const Projector3D& proj, const glm::vec3& originWorld, GizmoAxis activeAxis, ImVec2& outOriginScreen) {
            if (!proj.Project(originWorld, outOriginScreen))
                return GizmoAxis::None;

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 mouse = ImGui::GetIO().MousePos;

            struct AxisInfo { glm::vec3 Dir; ImU32 Color; ImU32 HighlightColor; GizmoAxis Axis; };
            AxisInfo axes[3] = {
                { { 1.0f, 0.0f, 0.0f }, IM_COL32(230, 70, 70, 255), IM_COL32(255, 150, 150, 255), GizmoAxis::X },
                { { 0.0f, 1.0f, 0.0f }, IM_COL32(80, 200, 100, 255), IM_COL32(160, 255, 180, 255), GizmoAxis::Y },
                { { 0.0f, 0.0f, 1.0f }, IM_COL32(80, 140, 230, 255), IM_COL32(160, 190, 255, 255), GizmoAxis::Z },
            };

            GizmoAxis hovered = GizmoAxis::None;
            float bestDist = Gizmo3DHitBand;

            if (mode == GizmoMode::Rotate) {
                constexpr int Segments = 32;
                for (auto& info : axes) {
                    glm::vec3 a = (std::abs(info.Dir.x) < 0.9f) ? glm::normalize(glm::cross(info.Dir, glm::vec3(1.0f, 0.0f, 0.0f)))
                                                                  : glm::normalize(glm::cross(info.Dir, glm::vec3(0.0f, 1.0f, 0.0f)));
                    glm::vec3 b = glm::cross(info.Dir, a);
                    bool isHighlighted = (activeAxis == info.Axis);

                    std::vector<ImVec2> points;
                    points.reserve(Segments + 1);
                    for (int i = 0; i <= Segments; i++) {
                        float t = (static_cast<float>(i) / Segments) * 2.0f * 3.14159265f;
                        glm::vec3 worldPt = originWorld + (a * std::cos(t) + b * std::sin(t)) * Gizmo3DAxisLength;
                        ImVec2 screenPt;
                        if (proj.Project(worldPt, screenPt))
                            points.push_back(screenPt);
                    }
                    if (points.size() < 2)
                        continue;

                    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), isHighlighted ? info.HighlightColor : info.Color, ImDrawFlags_None, 2.5f);

                    float ringMinDist = std::numeric_limits<float>::max();
                    for (const ImVec2& p : points) {
                        float dx = mouse.x - p.x, dy = mouse.y - p.y;
                        ringMinDist = std::min(ringMinDist, std::sqrt(dx * dx + dy * dy));
                    }
                    if (ringMinDist < bestDist) {
                        bestDist = ringMinDist;
                        hovered = info.Axis;
                    }
                }
                return hovered;
            }

            for (auto& info : axes) {
                ImVec2 screenTip;
                if (!proj.Project(originWorld + info.Dir * Gizmo3DAxisLength, screenTip))
                    continue;
                bool isHighlighted = (activeAxis == info.Axis);
                ImU32 color = isHighlighted ? info.HighlightColor : info.Color;
                drawList->AddLine(outOriginScreen, screenTip, color, 3.0f);
                if (mode == GizmoMode::Scale)
                    drawList->AddRectFilled(ImVec2(screenTip.x - Gizmo3DScaleHandleHalfSize, screenTip.y - Gizmo3DScaleHandleHalfSize),
                                             ImVec2(screenTip.x + Gizmo3DScaleHandleHalfSize, screenTip.y + Gizmo3DScaleHandleHalfSize), color);
                else
                    drawList->AddCircleFilled(screenTip, 5.0f, color);

                float dist = PointSegmentDistance(mouse, outOriginScreen, screenTip);
                if (dist < bestDist) {
                    bestDist = dist;
                    hovered = info.Axis;
                }
            }

            ImU32 centerColor = (activeAxis == GizmoAxis::Both) ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 210, 70, 255);
            drawList->AddCircleFilled(outOriginScreen, Gizmo3DCenterRadius, centerColor);
            float centerDx = mouse.x - outOriginScreen.x, centerDy = mouse.y - outOriginScreen.y;
            if (std::sqrt(centerDx * centerDx + centerDy * centerDy) <= Gizmo3DCenterRadius)
                hovered = GizmoAxis::Both;

            return hovered;
        }

        // Renders and interacts with one screen's 3D orbit-camera pane -- the 3D analog of the
        // 2D body below. Selecting a mesh sets ctx.Selected; a Translate/Rotate/Scale gizmo
        // (DrawAndHitTestGizmo3D above) then lets it be edited directly in the pane, same as 2D
        // -- the Properties panel's generic DragFloat3 fields remain available too either way.
        void DrawScenePane3D(EditorContext& ctx, Screen screen, SceneViewCamera3D& camera3D, OpenGLRenderer3D& renderer3D, Framebuffer& framebuffer, ImVec2 paneSize) {
            if (!camera3D.Seeded) {
                Entity primaryCamera = ctx.SceneRef.GetPrimaryCamera(screen);
                if (primaryCamera && primaryCamera.GetComponent<CameraComponent>().Projection == ProjectionType::Perspective) {
                    TransformComponent transform = ctx.SceneRef.GetWorldTransform(primaryCamera);
                    camera3D.Rotation = EulerDegreesToQuat({ transform.Rotation.x, transform.Rotation.y, 0.0f });
                    camera3D.Distance = 300.0f;
                    // Places this orbit camera at the exact same position/orientation as the
                    // real camera: Target is a point straight ahead of it, so
                    // Target - forward*Distance recovers transform.Translation exactly.
                    glm::vec3 seedForward = camera3D.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
                    camera3D.Target = transform.Translation + seedForward * camera3D.Distance;
                }
                camera3D.Seeded = true;
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            int viewportW = std::max(1, static_cast<int>(avail.x));
            int viewportH = std::max(1, static_cast<int>(avail.y));
            framebuffer.Resize(static_cast<uint32_t>(viewportW), static_cast<uint32_t>(viewportH));

            // Derived directly from the quaternion (never via cross(forward, worldUp)) so
            // right/up stay well-defined at any orientation, including forward == worldUp --
            // see SceneViewCamera3D.h's own comment on why this replaced Pitch/Yaw.
            glm::vec3 forward = camera3D.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 right = camera3D.Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 up = camera3D.Rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 cameraPos = camera3D.Target - forward * camera3D.Distance;
            glm::vec3 cameraRotation = QuatToEulerDegrees(camera3D.Rotation);
            float aspect = static_cast<float>(viewportW) / static_cast<float>(viewportH);
            constexpr float FovDegrees = 60.0f, NearPlane = 0.1f, FarPlane = 5000.0f;

            framebuffer.Bind();
            glm::vec4 clearColor = (screen == Screen::Top) ? glm::vec4{ 0.15f, 0.15f, 0.18f, 1.0f } : glm::vec4{ 0.18f, 0.15f, 0.15f, 1.0f };
            renderer3D.BeginScene(screen, ProjectionType::Perspective, cameraPos, cameraRotation, FovDegrees, 0.0f, aspect, NearPlane, FarPlane, clearColor, true);
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, MeshRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                // Inactive entities' rendered content (mesh/sprite) is hidden here too, not just
                // in the real Game view -- user-requested: unlike Unity's own Scene-view-shows-
                // disabled-objects convention, seeing an entity disappear the instant Active is
                // unchecked is the expected feedback here. Colliders/camera frustums/gizmos
                // deliberately stay visible regardless (still useful to see/edit while inactive).
                if (!ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef)))
                    continue;
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& mesh = ctx.SceneRef.Registry().get<MeshRendererComponent>(handle);
                uint32_t meshHandle = ResolveMeshGeometry(renderer3D, mesh.Mesh);
                // One draw call per submesh -- see SceneRenderer.cpp's RenderScreen3D, same loop.
                uint32_t subMeshCount = renderer3D.GetSubMeshCount(meshHandle);
                for (uint32_t i = 0; i < subMeshCount; i++) {
                    const AssetRef& materialRef = mesh.Materials.empty()
                        ? AssetRef{} : mesh.Materials[std::min<size_t>(i, mesh.Materials.size() - 1)];
                    Material material = ResolveMeshMaterial(materialRef);
                    uint32_t textureId = ResolveMeshTexture(renderer3D, material.Texture);
                    renderer3D.DrawMesh(mesh.Primitive, meshHandle, i, transform.Translation, transform.Rotation, transform.Scale, material.Color, textureId);
                }
            }
            // Sprites always visible here too now, drawn as flat upright quads (Plane rotated
            // 90 degrees around X, so its XZ-facing surface faces the camera instead of lying
            // flat) at Z=0 -- matches the real game's own "camera shows both 2D and 3D content"
            // convention (see SceneRenderer.cpp's RenderScreen), just via a screen-space-quad
            // stand-in since IRenderer3D has no dedicated 2D-quad-in-3D-space primitive. Real
            // depth-testing against meshes falls out for free (both go through the same
            // renderer/depth buffer here), unlike the fixed mesh-then-sprite draw order the
            // real game and the 2D pane below have to use instead (two separate renderers,
            // no shared depth buffer).
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                if (!ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef)))
                    continue; // see the mesh loop above for why this pane hides inactive entities too
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& sprite = ctx.SceneRef.Registry().get<SpriteRendererComponent>(handle);
                // Resolved via renderer3D's own texture cache, not ctx.Renderer's -- keeps this
                // draw call self-contained to the renderer it's actually issued through (a
                // harmless double-allocation if the same file is also drawn by a real sprite
                // elsewhere, same tolerance as Citro3DRenderer/Citro2DRenderer's own separate
                // texture caches).
                uint32_t textureId = ResolveMeshTexture(renderer3D, GetActiveSpriteTexture(ctx.SceneRef, handle));
                renderer3D.DrawMesh(MeshPrimitive::Plane, 0, 0,
                    { transform.Translation.x, transform.Translation.y, 0.0f },
                    { 90.0f, 0.0f, transform.Rotation.z },
                    { sprite.Size.x, 1.0f, sprite.Size.y },
                    sprite.Color, textureId);
            }
            // Cameras have no mesh of their own -- shown via the frustum wireframe overlay
            // below instead of a solid mesh marker here: this pane's own orbit camera is
            // deliberately SEEDED to sit at a Perspective camera's exact position (WYSIWYG
            // initial view, see camera3D.Seeded above), so a solid marker drawn at that same
            // position would put the viewer inside it, filling the whole view with the
            // marker's own color -- confirmed as a real bug before this fix. Wireframe lines
            // don't have that problem (nothing to "be inside of").
            renderer3D.EndScene();
            framebuffer.Unbind();

            ImVec2 imagePos = ImGui::GetCursorScreenPos();
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(framebuffer.GetColorAttachment())), avail, ImVec2(0, 1), ImVec2(1, 0));
            bool imageHovered = ImGui::IsItemHovered();
            ImGuiIO& io = ImGui::GetIO();

            Projector3D proj{ cameraPos, forward, right, up, FovDegrees, aspect, imagePos, viewportW, viewportH };

            // Unity/Cocos-style ground grid, drawn first (before every other overlay below) so
            // it visually reads as "underneath" the camera frustums/colliders/gizmos, even
            // though (like all of them) it's really just a flat screen-space overlay with no
            // true depth test against the rendered mesh scene.
            {
                float gridCellSize = GridCellSize3D(camera3D.Distance);
                // Scales with how far the camera actually is from its own focus point --
                // bounded to [10, 40] grid squares in each direction, rather than one fixed
                // patch size regardless of zoom (see DrawGrid3D's own comment on why that made
                // lines needlessly likely to require near-plane clipping as the camera moved).
                float gridExtent = std::clamp(camera3D.Distance * 3.0f, gridCellSize * 10.0f, gridCellSize * 40.0f);
                DrawGrid3D(proj, camera3D.Target, gridCellSize, gridExtent);
            }

            // Frustum wireframe for every camera entity, showing at a glance where it's
            // looking and its Fov/Zoom -- same color-by-target-screen convention as the sphere
            // marker/2D pane's own camera marker. Roll (Rotation.z) is ignored for this gizmo's
            // own forward/right/up (same simplification OrbitForward already makes) -- it only
            // rotates the frustum rectangle around its own view axis, not worth the extra
            // precision for a purely visual aid.
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CameraComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                TransformComponent camTransform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& cameraComponent = ctx.SceneRef.Registry().get<CameraComponent>(handle);
                glm::vec3 camForward = OrbitForward(camTransform.Rotation.x, camTransform.Rotation.y);
                glm::vec3 camRight = glm::normalize(glm::cross(camForward, glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::vec3 camUp = glm::cross(camRight, camForward);
                float realScreenHeight = (screen == Screen::Top) ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);
                float camOrthoHalfHeight = (realScreenHeight * 0.5f) / cameraComponent.Zoom; // matches SceneRenderer.cpp's own Zoom convention
                ImU32 frustumColor = (cameraComponent.Screen == Screen::Top) ? IM_COL32(80, 230, 230, 200) : IM_COL32(240, 150, 50, 200);
                DrawCameraFrustum(proj, camTransform.Translation, camForward, camRight, camUp, cameraComponent.Projection, cameraComponent.FovDegrees, camOrthoHalfHeight, aspect, cameraComponent.NearPlane, cameraComponent.FarPlane, frustumColor);
            }

            // 3D collider gizmo -- green wireframe, editor-only (never actually rendered by the
            // real IRenderer3D pass), drawn ONLY for the selected entity (previously drawn for
            // every collider in the scene at once, which got visually noisy fast in anything
            // beyond a small demo scene). Draggable resize handles only respond once the
            // component's own "Edit" checkbox (Properties panel) is on, so a collider isn't
            // accidentally reshaped by a stray drag while just moving/inspecting the entity --
            // the wireframe+handle markers themselves still draw regardless of Edit, matching
            // Unity's own "always show the gizmo, only let you grab it in edit mode" feel.
            if (ctx.Selected && ctx.Selected.HasComponent<BoxCollider3DComponent>() && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<BoxCollider3DComponent>();
                glm::vec3 center = transform.Translation + collider.Offset;
                glm::quat rotation = EulerDegreesToQuat(transform.Rotation);

                ImVec2 faceHandles[3];
                DrawBoxCollider3D(proj, center, rotation, collider.Size, faceHandles);

                if (collider.EditMode) {
                    ImVec2 centerScreen;
                    if (proj.Project(center, centerScreen)) {
                        const char* ids[3] = { "##ColliderResizeX", "##ColliderResizeY", "##ColliderResizeZ" };
                        for (int i = 0; i < 3; i++) {
                            if (std::isnan(faceHandles[i].x))
                                continue;
                            float newValue = DragCollider3DHandle(ids[i], centerScreen, faceHandles[i], camera3D.Distance, collider.Size[i]);
                            if (newValue != collider.Size[i])
                                collider.Size[i] = newValue;
                        }
                    }
                }
            }
            if (ctx.Selected && ctx.Selected.HasComponent<SphereCollider3DComponent>() && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<SphereCollider3DComponent>();
                glm::vec3 center = transform.Translation + collider.Offset;

                ImVec2 equatorHandle;
                DrawSphereCollider3D(proj, center, collider.Radius, equatorHandle);

                if (collider.EditMode && !std::isnan(equatorHandle.x)) {
                    ImVec2 centerScreen;
                    if (proj.Project(center, centerScreen)) {
                        float newRadius = DragCollider3DHandle("##ColliderResizeRadius", centerScreen, equatorHandle, camera3D.Distance, collider.Radius);
                        if (newRadius != collider.Radius)
                            collider.Radius = newRadius;
                    }
                }
            }

            // Draw+hit-test the gizmo every frame (not just while hovered), matching the 2D
            // pane's own reasoning -- a drag already in progress keeps tracking even if the
            // mouse drifts outside the image mid-drag. Cameras get a gizmo too, same as any
            // other 3D object (Unity's own convention) -- picking/moving a camera in the Scene
            // view shouldn't need a different tool than picking/moving a mesh.
            bool hasGizmoTarget = ctx.Selected && ctx.Selected.HasComponent<TransformComponent>() &&
                (ctx.Selected.HasComponent<MeshRendererComponent>() || ctx.Selected.HasComponent<CameraComponent>());
            GizmoAxis hoveredGizmoAxis = GizmoAxis::None;
            ImVec2 gizmoOriginScreen{};
            if (hasGizmoTarget) {
                TransformComponent selectedWorld = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                GizmoAxis activeAxisForThisPane = (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen != screen)
                    ? GizmoAxis::None : ctx.DraggingGizmoAxis;
                hoveredGizmoAxis = DrawAndHitTestGizmo3D(ctx.ActiveGizmoMode, proj, selectedWorld.Translation, activeAxisForThisPane, gizmoOriginScreen);
            }

            // Drawn last (on top of everything else in this pane -- a fixed screen-space
            // widget, unlike every other overlay above which is anchored to world content) and
            // hit-tested BEFORE the entity-pick-ray below, so clicking it snaps the view instead
            // of also selecting/deselecting whatever mesh happens to be underneath it.
            bool orientationGizmoConsumedClick = DrawAndInteractOrientationGizmo3D(camera3D, imagePos, viewportW, imageHovered);

            if (imageHovered) {
                if (io.MouseWheel != 0.0f)
                    camera3D.Distance = std::clamp(camera3D.Distance * (1.0f - io.MouseWheel * 0.1f), 10.0f, 5000.0f);

                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                    float panScale = camera3D.Distance * 0.0015f;
                    camera3D.Target -= right * io.MouseDelta.x * panScale;
                    camera3D.Target += up * io.MouseDelta.y * panScale;
                }

                // Right-drag orbits, matching Unity's Scene view convention (left is reserved
                // for selection/gizmo-dragging, middle pans, wheel zooms -- all three mouse
                // buttons doing something distinct, like Unity). Yaw is applied as a world-space
                // rotation (pre-multiplied) around the fixed world-up axis, so horizontal drag
                // always swings the view around the same "up" regardless of current pitch --
                // pitch is applied as a local-space rotation (post-multiplied) around the
                // camera's own current right axis. No clamp: unlike the old pitch/yaw + world-up
                // cross-product scheme, this quaternion composition has no singularity, so the
                // camera flips smoothly through the poles like Unity/Blender's Scene view
                // instead of stopping at +-89 degrees.
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                    glm::quat yawRot = glm::angleAxis(glm::radians(io.MouseDelta.x * 0.3f), glm::vec3(0.0f, 1.0f, 0.0f));
                    glm::quat pitchRot = glm::angleAxis(glm::radians(-io.MouseDelta.y * 0.3f), glm::vec3(1.0f, 0.0f, 0.0f));
                    camera3D.Rotation = glm::normalize(yawRot * camera3D.Rotation * pitchRot);
                }

                if (!orientationGizmoConsumedClick && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (hasGizmoTarget && hoveredGizmoAxis != GizmoAxis::None) {
                        ctx.DraggingGizmoAxis = hoveredGizmoAxis;
                        ctx.DraggingGizmoScreen = screen;
                    } else {
                        float ndcX = (2.0f * ((io.MousePos.x - imagePos.x) / viewportW)) - 1.0f;
                        float ndcY = 1.0f - (2.0f * ((io.MousePos.y - imagePos.y) / viewportH));
                        float tanHalfFov = std::tan(glm::radians(FovDegrees) * 0.5f);
                        glm::vec3 rayDir = glm::normalize(forward + right * (ndcX * tanHalfFov * aspect) + up * (ndcY * tanHalfFov));

                        Entity hit;
                        float closestT = std::numeric_limits<float>::max();
                        auto testSphere = [&](Entity candidate, const glm::vec3& center, float radius) {
                            glm::vec3 toSphere = center - cameraPos;
                            float tClosest = glm::dot(toSphere, rayDir);
                            if (tClosest < 0.0f)
                                return; // behind the camera
                            glm::vec3 closestPoint = cameraPos + rayDir * tClosest;
                            if (glm::distance(closestPoint, center) <= radius && tClosest < closestT) {
                                closestT = tClosest;
                                hit = candidate;
                            }
                        };

                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, MeshRendererComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            if (!ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef)))
                                continue; // not drawn -- shouldn't be clickable here either
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& mesh = candidate.GetComponent<MeshRendererComponent>();
                            float maxScale = std::max({ std::abs(transform.Scale.x), std::abs(transform.Scale.y), std::abs(transform.Scale.z) });
                            testSphere(candidate, transform.Translation, MeshBoundingRadius(mesh) * maxScale);
                        }
                        // Cameras are pickable in this pane too now (Unity's own convention --
                        // any 3D object, camera included, is selectable/movable from the Scene
                        // view), using a fixed marker radius since a camera has no mesh/bounds
                        // of its own to measure.
                        constexpr float CameraPickRadius = 20.0f;
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CameraComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            testSphere(candidate, transform.Translation, CameraPickRadius);
                        }
                        if (hit)
                            ctx.Selected = hit;
                    }
                }
            }

            // Not gated on imageHovered -- once a drag starts it should keep following the
            // mouse even if the cursor leaves the image, matching the 2D pane's own gizmo drag.
            if (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen == screen) {
                if (hasGizmoTarget && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    auto& selectedTransform = ctx.Selected.GetComponent<TransformComponent>();
                    glm::vec2 mouseDelta{ io.MouseDelta.x, io.MouseDelta.y };

                    if (ctx.ActiveGizmoMode == GizmoMode::Rotate) {
                        ImVec2 prevMouse{ io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y };
                        float angleNow = std::atan2(io.MousePos.y - gizmoOriginScreen.y, io.MousePos.x - gizmoOriginScreen.x);
                        float anglePrev = std::atan2(prevMouse.y - gizmoOriginScreen.y, prevMouse.x - gizmoOriginScreen.x);
                        float deltaDegrees = glm::degrees(angleNow - anglePrev);
                        while (deltaDegrees > 180.0f) deltaDegrees -= 360.0f;
                        while (deltaDegrees < -180.0f) deltaDegrees += 360.0f;
                        if (ctx.DraggingGizmoAxis == GizmoAxis::X) selectedTransform.Rotation.x += deltaDegrees;
                        else if (ctx.DraggingGizmoAxis == GizmoAxis::Y) selectedTransform.Rotation.y += deltaDegrees;
                        else if (ctx.DraggingGizmoAxis == GizmoAxis::Z) selectedTransform.Rotation.z += deltaDegrees;
                    } else {
                        // Translate/Scale: project mouseDelta onto each axis's own screen-space
                        // direction (recomputed fresh every frame so it stays correct as the
                        // camera orbits), then apply the resulting scalar along that WORLD axis.
                        auto axisScreenDelta = [&](const glm::vec3& axisDir) -> float {
                            ImVec2 tipScreen;
                            if (!proj.Project(selectedTransform.Translation + axisDir * Gizmo3DAxisLength, tipScreen))
                                return 0.0f;
                            glm::vec2 axisDir2D{ tipScreen.x - gizmoOriginScreen.x, tipScreen.y - gizmoOriginScreen.y };
                            float len = glm::length(axisDir2D);
                            if (len < 1e-4f)
                                return 0.0f;
                            return glm::dot(mouseDelta, axisDir2D / len);
                        };

                        if (ctx.ActiveGizmoMode == GizmoMode::Scale) {
                            if (ctx.DraggingGizmoAxis == GizmoAxis::X) selectedTransform.Scale.x += axisScreenDelta({ 1.0f, 0.0f, 0.0f }) * ScaleDragSensitivity;
                            else if (ctx.DraggingGizmoAxis == GizmoAxis::Y) selectedTransform.Scale.y += axisScreenDelta({ 0.0f, 1.0f, 0.0f }) * ScaleDragSensitivity;
                            else if (ctx.DraggingGizmoAxis == GizmoAxis::Z) selectedTransform.Scale.z += axisScreenDelta({ 0.0f, 0.0f, 1.0f }) * ScaleDragSensitivity;
                            else if (ctx.DraggingGizmoAxis == GizmoAxis::Both)
                                selectedTransform.Scale += glm::vec3((mouseDelta.x - mouseDelta.y) * ScaleDragSensitivity * 0.5f);
                        } else {
                            // World-units-per-pixel scaled by distance from camera, same idea
                            // as the orbit camera's own pan scale above.
                            float worldPerPixel = camera3D.Distance * 0.003f;
                            if (ctx.DraggingGizmoAxis == GizmoAxis::X) selectedTransform.Translation.x += axisScreenDelta({ 1.0f, 0.0f, 0.0f }) * worldPerPixel;
                            else if (ctx.DraggingGizmoAxis == GizmoAxis::Y) selectedTransform.Translation.y += axisScreenDelta({ 0.0f, 1.0f, 0.0f }) * worldPerPixel;
                            else if (ctx.DraggingGizmoAxis == GizmoAxis::Z) selectedTransform.Translation.z += axisScreenDelta({ 0.0f, 0.0f, 1.0f }) * worldPerPixel;
                            else if (ctx.DraggingGizmoAxis == GizmoAxis::Both)
                                selectedTransform.Translation += right * mouseDelta.x * worldPerPixel - up * mouseDelta.y * worldPerPixel;
                        }
                    }
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    ctx.DraggingGizmoAxis = GizmoAxis::None;
            }
        }

        // Renders and interacts with one screen's 2D edit-camera pane -- called once
        // for Top, once for Bottom (see DrawScenePane below). Selection
        // (ctx.Selected) and the active gizmo tool (ctx.ActiveGizmoMode) are shared
        // across both panes (one selection, one gizmo, editable from either side);
        // only the camera (pan/zoom) and the actual drag-in-progress are per-pane --
        // see ctx.DraggingGizmoScreen's own comment for why a drag needs to
        // remember which pane started it.
        void DrawScenePane2D(EditorContext& ctx, Screen screen, SceneViewCamera& camera, Framebuffer& framebuffer, ImVec2 paneSize) {
            // Seeded once from the screen's real primary CameraComponent (if any) so
            // the initial view looks WYSIWYG -- never re-seeded afterward, so Play
            // mode moving the real camera doesn't yank the edit view out from under
            // the user (matches Unity's Scene-vs-Game camera separation).
            if (!camera.Seeded) {
                Entity primaryCamera = ctx.SceneRef.GetPrimaryCamera(screen);
                if (primaryCamera) {
                    auto& transform = primaryCamera.GetComponent<TransformComponent>();
                    camera.Position = { transform.Translation.x, transform.Translation.y };
                    camera.Zoom = primaryCamera.GetComponent<CameraComponent>().Zoom;
                } else {
                    float defaultWidth = (screen == Screen::Top) ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
                    float defaultHeight = (screen == Screen::Top) ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);
                    camera.Position = { defaultWidth * 0.5f, defaultHeight * 0.5f };
                    camera.Zoom = 1.0f;
                }
                camera.Seeded = true;
            }


            ImVec2 avail = ImGui::GetContentRegionAvail();
            int viewportW = std::max(1, static_cast<int>(avail.x));
            int viewportH = std::max(1, static_cast<int>(avail.y));
            framebuffer.Resize(static_cast<uint32_t>(viewportW), static_cast<uint32_t>(viewportH));

            // BeginCustomView's projection is already centered on camera.Position/
            // camera.Zoom, so DrawQuad calls below use plain world coordinates --
            // manually re-applying the camera offset/zoom on top of that would
            // double-transform every position, drifting away from the gizmo/
            // selection math below (which already computes a single correct
            // transform) the more the camera pans or zooms from its default.
            framebuffer.Bind();
            glm::vec4 clearColor = (screen == Screen::Top) ? glm::vec4{ 0.15f, 0.15f, 0.18f, 1.0f } : glm::vec4{ 0.18f, 0.15f, 0.15f, 1.0f };

            // 3D mesh content always visible here too now, drawn first (and clearing the
            // screen) via an Orthographic projection built from this SAME 2D camera's own
            // Position/Zoom -- sprites draw on top of it afterward without re-clearing, matching
            // the real game's own fixed "mesh behind, sprite in front" draw order (see
            // SceneRenderer.cpp's RenderScreen) since these are two separate renderers with no
            // shared depth buffer to sort against each other properly.
            glm::vec3 orthoCameraPos{ camera.Position.x, camera.Position.y, 1000.0f };
            float orthoHalfHeight = (static_cast<float>(viewportH) * 0.5f) / camera.Zoom;
            float orthoAspect = static_cast<float>(viewportW) / static_cast<float>(viewportH);
            ctx.Renderer3D.BeginScene(screen, ProjectionType::Orthographic, orthoCameraPos, glm::vec3(0.0f), 60.0f, orthoHalfHeight, orthoAspect, 0.1f, 5000.0f, clearColor, true);
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, MeshRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                if (!ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef)))
                    continue; // see DrawScenePane3D's own mesh loop for why this pane hides inactive entities too
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& mesh = ctx.SceneRef.Registry().get<MeshRendererComponent>(handle);
                uint32_t meshHandle = ResolveMeshGeometry(ctx.Renderer3D, mesh.Mesh);
                uint32_t subMeshCount = ctx.Renderer3D.GetSubMeshCount(meshHandle);
                for (uint32_t i = 0; i < subMeshCount; i++) {
                    const AssetRef& materialRef = mesh.Materials.empty()
                        ? AssetRef{} : mesh.Materials[std::min<size_t>(i, mesh.Materials.size() - 1)];
                    Material material = ResolveMeshMaterial(materialRef);
                    uint32_t textureId = ResolveMeshTexture(ctx.Renderer3D, material.Texture);
                    ctx.Renderer3D.DrawMesh(mesh.Primitive, meshHandle, i, transform.Translation, transform.Rotation, transform.Scale, material.Color, textureId);
                }
            }
            ctx.Renderer3D.EndScene();

            ctx.Renderer.BeginCustomView(camera.Position, camera.Zoom, static_cast<float>(viewportW), static_cast<float>(viewportH), clearColor, false);
            // Unity/Cocos-style Scene view grid -- drawn via real GL calls (not a screen-space
            // ImDrawList overlay) specifically so it renders BEHIND sprites rather than on top
            // of them; an overlay drawn after ImGui::Image() below would sit on top of the
            // whole already-composited framebuffer, including every opaque sprite.
            ctx.Renderer.DrawGrid(camera.Position, camera.Zoom, static_cast<float>(viewportW), static_cast<float>(viewportH), GridCellSize2D(camera.Zoom));
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                if (!ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef)))
                    continue; // see DrawScenePane3D's own mesh loop for why this pane hides inactive entities too
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& sprite = ctx.SceneRef.Registry().get<SpriteRendererComponent>(handle);
                glm::vec2 topLeft{ transform.Translation.x - sprite.Size.x * 0.5f, transform.Translation.y - sprite.Size.y * 0.5f };
                uint32_t textureId = ResolveSpriteTexture(ctx.Renderer, GetActiveSpriteTexture(ctx.SceneRef, handle));
                ctx.Renderer.DrawQuad(topLeft, sprite.Size, sprite.Color, transform.Rotation.z, textureId);
            }
            // Cameras have no sprite of their own -- draw a small gizmo marker at
            // each one's position (color-coded by target screen), sized in world
            // units scaled inversely by zoom so it reads as a constant screen size
            // rather than shrinking/growing with zoom. A camera always resolves to
            // its own CameraComponent::Screen (checked first in
            // TryResolveEntityScreen), so this naturally only shows a screen's own
            // camera in its own pane.
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CameraComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& cameraComponent = ctx.SceneRef.Registry().get<CameraComponent>(handle);
                float markerSize = 14.0f / camera.Zoom;
                glm::vec4 markerColor = (cameraComponent.Screen == Screen::Top) ? glm::vec4{ 0.3f, 0.9f, 0.9f, 1.0f } : glm::vec4{ 0.95f, 0.6f, 0.2f, 1.0f };
                ctx.Renderer.DrawQuad({ transform.Translation.x - markerSize * 0.5f, transform.Translation.y - markerSize * 0.5f }, { markerSize, markerSize }, markerColor);
            }
            ctx.Renderer.EndScene();
            framebuffer.Unbind();

            ImVec2 imagePos = ImGui::GetCursorScreenPos();
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(framebuffer.GetColorAttachment())), avail, ImVec2(0, 1), ImVec2(1, 0));
            bool imageHovered = ImGui::IsItemHovered();
            ImGuiIO& io = ImGui::GetIO();

            auto worldToPaneScreen = [&](const glm::vec2& worldPos) {
                return ImVec2(
                    imagePos.x + (worldPos.x - camera.Position.x) * camera.Zoom + viewportW * 0.5f,
                    imagePos.y + (worldPos.y - camera.Position.y) * camera.Zoom + viewportH * 0.5f);
            };

            // Collider gizmo -- Unity/Unreal-style green wireframe outline, drawn ONLY for the
            // selected entity (previously drawn for every collider in the scene at once, which
            // got visually noisy fast in anything beyond a small demo scene), on top of the
            // rendered sprites but under the translate/rotate/scale gizmo below. Purely an
            // editor-only overlay (ImDrawList, like the translate/rotate/scale gizmo) --
            // colliders are never actually rendered by the real IRenderer2D pass, on desktop or
            // on-device, same as Unity's own Gizmos only ever showing in the Scene view. Small
            // square resize handles are drawn alongside the outline whenever selected, but only
            // respond to a drag once the component's own "Edit" checkbox (Properties panel) is
            // on -- see DragCollider2DHandle's own comment for why.
            ImDrawList* colliderDrawList = ImGui::GetWindowDrawList();
            const ImU32 colliderColor = IM_COL32(60, 230, 90, 255);
            // Offset is added in world space untransformed by the parent's rotation --
            // a tiny, purely-visual imprecision for rotated parents (colliders are never
            // simulated as children of a moving parent anyway, see Scene::OnRuntimeStart).
            if (ctx.Selected && ctx.Selected.HasComponent<BoxCollider2DComponent>() && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<BoxCollider2DComponent>();
                glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                ImVec2 topLeft = worldToPaneScreen(center - collider.Size);
                ImVec2 bottomRight = worldToPaneScreen(center + collider.Size);
                colliderDrawList->AddRect(topLeft, bottomRight, colliderColor, 0.0f, 0, 2.0f);

                ImVec2 centerScreen = worldToPaneScreen(center);
                ImVec2 xHandle = worldToPaneScreen(center + glm::vec2{ collider.Size.x, 0.0f });
                ImVec2 yHandle = worldToPaneScreen(center + glm::vec2{ 0.0f, collider.Size.y });
                colliderDrawList->AddRectFilled(ImVec2(xHandle.x - Collider2DHandleHalfSize, xHandle.y - Collider2DHandleHalfSize),
                    ImVec2(xHandle.x + Collider2DHandleHalfSize, xHandle.y + Collider2DHandleHalfSize), colliderColor);
                colliderDrawList->AddRectFilled(ImVec2(yHandle.x - Collider2DHandleHalfSize, yHandle.y - Collider2DHandleHalfSize),
                    ImVec2(yHandle.x + Collider2DHandleHalfSize, yHandle.y + Collider2DHandleHalfSize), colliderColor);

                if (collider.EditMode) {
                    float newX = DragCollider2DHandle("##Collider2DResizeX", centerScreen, xHandle, camera.Zoom, collider.Size.x);
                    if (newX != collider.Size.x)
                        collider.Size.x = newX;
                    float newY = DragCollider2DHandle("##Collider2DResizeY", centerScreen, yHandle, camera.Zoom, collider.Size.y);
                    if (newY != collider.Size.y)
                        collider.Size.y = newY;
                }
            }
            if (ctx.Selected && ctx.Selected.HasComponent<CircleCollider2DComponent>() && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<CircleCollider2DComponent>();
                glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                ImVec2 screenCenter = worldToPaneScreen(center);
                colliderDrawList->AddCircle(screenCenter, collider.Radius * camera.Zoom, colliderColor, 32, 2.0f);

                ImVec2 radiusHandle = worldToPaneScreen(center + glm::vec2{ collider.Radius, 0.0f });
                colliderDrawList->AddRectFilled(ImVec2(radiusHandle.x - Collider2DHandleHalfSize, radiusHandle.y - Collider2DHandleHalfSize),
                    ImVec2(radiusHandle.x + Collider2DHandleHalfSize, radiusHandle.y + Collider2DHandleHalfSize), colliderColor);

                if (collider.EditMode) {
                    float newRadius = DragCollider2DHandle("##Collider2DResizeRadius", screenCenter, radiusHandle, camera.Zoom, collider.Radius);
                    if (newRadius != collider.Radius)
                        collider.Radius = newRadius;
                }
            }

            // Draw+hit-test the gizmo every frame (not just while hovered) so a
            // drag already in progress keeps tracking even if the mouse drifts
            // outside the image mid-drag. Drawn in both panes (the selected
            // entity is shared, editable from whichever pane is more convenient).
            bool hasGizmoTarget = ctx.Selected && ctx.Selected.HasComponent<TransformComponent>();
            GizmoAxis hoveredGizmoAxis = GizmoAxis::None;
            ImVec2 gizmoOrigin{};
            if (hasGizmoTarget) {
                TransformComponent selectedWorld = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                gizmoOrigin = worldToPaneScreen({ selectedWorld.Translation.x, selectedWorld.Translation.y });
                // Only the pane that started the current drag reports a hovered
                // handle -- otherwise moving the mouse over the OTHER pane while
                // dragging in this one would steal the highlight/hit-test.
                GizmoAxis activeAxisForThisPane = (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen != screen)
                    ? GizmoAxis::None : ctx.DraggingGizmoAxis;
                hoveredGizmoAxis = DrawAndHitTestGizmo2D(ctx.ActiveGizmoMode, gizmoOrigin, activeAxisForThisPane);
            }

            if (imageHovered) {
                if (io.MouseWheel != 0.0f)
                    camera.Zoom = std::clamp(camera.Zoom * (1.0f + io.MouseWheel * 0.1f), 0.1f, 5.0f);

                // Middle-mouse-drag pans, matching MyGameEngine's own Scene
                // view and Unity's convention (right-click is left unbound,
                // same as the reference -- it was only ever repurposed here as
                // an interim stand-in before this fix).
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                    camera.Position.x -= io.MouseDelta.x / camera.Zoom;
                    camera.Position.y -= io.MouseDelta.y / camera.Zoom;
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (hasGizmoTarget && hoveredGizmoAxis != GizmoAxis::None) {
                        ctx.DraggingGizmoAxis = hoveredGizmoAxis;
                        ctx.DraggingGizmoScreen = screen;
                    } else {
                        glm::vec2 local{ io.MousePos.x - imagePos.x, io.MousePos.y - imagePos.y };
                        glm::vec2 worldPoint{
                            (local.x - viewportW * 0.5f) / camera.Zoom + camera.Position.x,
                            (local.y - viewportH * 0.5f) / camera.Zoom + camera.Position.y
                        };

                        Entity hit;
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue; // not visible in this pane -- shouldn't be clickable here either
                            if (!ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef)))
                                continue; // same -- inactive entities aren't drawn here, so not clickable either
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& sprite = candidate.GetComponent<SpriteRendererComponent>();
                            bool inside = worldPoint.x >= transform.Translation.x - sprite.Size.x * 0.5f &&
                                          worldPoint.x <= transform.Translation.x + sprite.Size.x * 0.5f &&
                                          worldPoint.y >= transform.Translation.y - sprite.Size.y * 0.5f &&
                                          worldPoint.y <= transform.Translation.y + sprite.Size.y * 0.5f;
                            if (inside)
                                hit = candidate; // topmost (last drawn) match wins
                        }
                        if (hit)
                            ctx.Selected = hit;
                    }
                }
            }

            // Not gated on imageHovered: once a drag starts it should keep
            // following the mouse even if the cursor leaves the image rect,
            // matching how ImGui's own drag widgets behave. Gated to the pane
            // that actually started the drag (ctx.DraggingGizmoScreen) so a fast
            // mouse movement into the other pane mid-drag doesn't reinterpret the
            // drag using this pane's (wrong) camera pan/zoom.
            if (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen == screen) {
                if (hasGizmoTarget && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    auto& selectedTransform = ctx.Selected.GetComponent<TransformComponent>();

                    if (ctx.ActiveGizmoMode == GizmoMode::Rotate) {
                        // Angle delta between this frame's and last frame's
                        // mouse position, both measured from the gizmo's
                        // origin -- normalized into [-180, 180] so crossing
                        // atan2's -180/180 discontinuity behind the origin
                        // doesn't register as a ~360 degree jump.
                        ImVec2 prevMouse{ io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y };
                        float angleNow = std::atan2(io.MousePos.y - gizmoOrigin.y, io.MousePos.x - gizmoOrigin.x);
                        float anglePrev = std::atan2(prevMouse.y - gizmoOrigin.y, prevMouse.x - gizmoOrigin.x);
                        float deltaDegrees = glm::degrees(angleNow - anglePrev);
                        while (deltaDegrees > 180.0f) deltaDegrees -= 360.0f;
                        while (deltaDegrees < -180.0f) deltaDegrees += 360.0f;
                        selectedTransform.Rotation.z += deltaDegrees;
                    } else {
                        glm::vec2 worldDelta{ io.MouseDelta.x / camera.Zoom, io.MouseDelta.y / camera.Zoom };
                        // The gizmo drag is a world-space mouse delta, but it's being
                        // added to a LOCAL Translation/Scale -- converted so dragging
                        // feels the same regardless of the entity's parent (identity
                        // conversion for a root entity).
                        glm::vec2 localDelta = WorldDeltaToLocal(ctx.SceneRef, ctx.Selected, worldDelta);
                        if (ctx.ActiveGizmoMode == GizmoMode::Scale) {
                            if (ctx.DraggingGizmoAxis == GizmoAxis::X || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                                selectedTransform.Scale.x += localDelta.x * ScaleDragSensitivity;
                            if (ctx.DraggingGizmoAxis == GizmoAxis::Y || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                                selectedTransform.Scale.y += localDelta.y * ScaleDragSensitivity;
                        } else {
                            if (ctx.DraggingGizmoAxis == GizmoAxis::X || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                                selectedTransform.Translation.x += localDelta.x;
                            if (ctx.DraggingGizmoAxis == GizmoAxis::Y || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                                selectedTransform.Translation.y += localDelta.y;
                        }
                    }
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    ctx.DraggingGizmoAxis = GizmoAxis::None;
            }
        }

        // Shared per-pane chrome (label + 2D/3D toggle), then dispatches to
        // DrawScenePane2D or DrawScenePane3D above -- a pane renders through exactly
        // one pipeline at a time, never both composited together (see RenderMode).
        void DrawScenePane(EditorContext& ctx, const char* label, Screen screen, SceneViewCamera& camera, SceneViewCamera3D& camera3D, RenderMode& renderMode, Framebuffer& framebuffer, ImVec2 paneSize) {
            ImGui::Text("%s", label);
            ImGui::SameLine();

            auto renderModeButton = [&](const char* buttonLabel, RenderMode mode) {
                bool active = (renderMode == mode);
                if (active)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if (ImGui::SmallButton(buttonLabel))
                    renderMode = mode;
                if (active)
                    ImGui::PopStyleColor();
                ImGui::SameLine();
            };
            renderModeButton("2D", RenderMode::Mode2D);
            renderModeButton("3D", RenderMode::Mode3D);
            ImGui::NewLine();

            if (renderMode == RenderMode::Mode2D)
                DrawScenePane2D(ctx, screen, camera, framebuffer, paneSize);
            else
                DrawScenePane3D(ctx, screen, camera3D, ctx.Renderer3D, framebuffer, paneSize);
        }
    }

    void ScenePanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Scene");

        // Gizmo tool switcher -- Unity/MyGameEngine's Q/W/E/R convention,
        // minus a "None" mode (Translate is always at least available). Shared
        // across both panes below (one selection, one gizmo).
        auto modeButton = [&](const char* label, GizmoMode mode) {
            bool active = (ctx.ActiveGizmoMode == mode);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label))
                ctx.ActiveGizmoMode = mode;
            if (active)
                ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        modeButton("Translate", GizmoMode::Translate);
        modeButton("Rotate", GizmoMode::Rotate);
        modeButton("Scale", GizmoMode::Scale);
        ImGui::NewLine();

        // Two side-by-side panes, one per screen -- fixed 50/50 split (a
        // draggable splitter is a nice-to-have, not built yet, see ROADMAP.md).
        ImVec2 avail = ImGui::GetContentRegionAvail();
        constexpr float PaneSpacing = 4.0f;
        ImVec2 paneSize{ std::max(1.0f, (avail.x - PaneSpacing) * 0.5f), std::max(1.0f, avail.y) };

        ImGui::BeginChild("TopScenePane", paneSize, true);
        DrawScenePane(ctx, "Top Screen", Screen::Top, ctx.TopSceneView, ctx.TopSceneView3D, ctx.TopRenderMode, ctx.TopSceneFramebuffer, paneSize);
        ImGui::EndChild();

        ImGui::SameLine(0.0f, PaneSpacing);

        ImGui::BeginChild("BottomScenePane", paneSize, true);
        DrawScenePane(ctx, "Bottom Screen", Screen::Bottom, ctx.BottomSceneView, ctx.BottomSceneView3D, ctx.BottomRenderMode, ctx.BottomSceneFramebuffer, paneSize);
        ImGui::EndChild();

        ImGui::End();
    }

}
