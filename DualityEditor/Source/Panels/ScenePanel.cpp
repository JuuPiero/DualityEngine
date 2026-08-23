#include "DualityEditor/Panels/ScenePanel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneGizmo.h"
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
                float viewZ = -glm::dot(rel, Forward);
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
                    camera3D.Pitch = transform.Rotation.x;
                    camera3D.Yaw = transform.Rotation.y;
                    camera3D.Distance = 300.0f;
                    // Places this orbit camera at the exact same position/orientation as the
                    // real camera: Target is a point straight ahead of it, so
                    // Target - forward*Distance recovers transform.Translation exactly (see
                    // OrbitForward's own comment for the composition this relies on).
                    camera3D.Target = transform.Translation + OrbitForward(camera3D.Pitch, camera3D.Yaw) * camera3D.Distance;
                }
                camera3D.Seeded = true;
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            int viewportW = std::max(1, static_cast<int>(avail.x));
            int viewportH = std::max(1, static_cast<int>(avail.y));
            framebuffer.Resize(static_cast<uint32_t>(viewportW), static_cast<uint32_t>(viewportH));

            glm::vec3 forward = OrbitForward(camera3D.Pitch, camera3D.Yaw);
            glm::vec3 cameraPos = camera3D.Target - forward * camera3D.Distance;
            glm::vec3 cameraRotation{ camera3D.Pitch, camera3D.Yaw, 0.0f };
            float aspect = static_cast<float>(viewportW) / static_cast<float>(viewportH);
            constexpr float FovDegrees = 60.0f, NearPlane = 0.1f, FarPlane = 5000.0f;

            framebuffer.Bind();
            glm::vec4 clearColor = (screen == Screen::Top) ? glm::vec4{ 0.15f, 0.15f, 0.18f, 1.0f } : glm::vec4{ 0.18f, 0.15f, 0.15f, 1.0f };
            renderer3D.BeginScene(screen, cameraPos, cameraRotation, FovDegrees, aspect, NearPlane, FarPlane, clearColor);
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, MeshRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& mesh = ctx.SceneRef.Registry().get<MeshRendererComponent>(handle);
                uint32_t textureId = ResolveMeshTexture(renderer3D, mesh.Texture);
                renderer3D.DrawMesh(mesh.Primitive, transform.Translation, transform.Rotation, transform.Scale, mesh.Color, textureId);
            }
            renderer3D.EndScene();
            framebuffer.Unbind();

            ImVec2 imagePos = ImGui::GetCursorScreenPos();
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(framebuffer.GetColorAttachment())), avail, ImVec2(0, 1), ImVec2(1, 0));
            bool imageHovered = ImGui::IsItemHovered();
            ImGuiIO& io = ImGui::GetIO();
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up = glm::cross(right, forward);

            Projector3D proj{ cameraPos, forward, right, up, FovDegrees, aspect, imagePos, viewportW, viewportH };

            // Draw+hit-test the gizmo every frame (not just while hovered), matching the 2D
            // pane's own reasoning -- a drag already in progress keeps tracking even if the
            // mouse drifts outside the image mid-drag.
            bool hasGizmoTarget = ctx.Selected && ctx.Selected.HasComponent<TransformComponent>() && ctx.Selected.HasComponent<MeshRendererComponent>();
            GizmoAxis hoveredGizmoAxis = GizmoAxis::None;
            ImVec2 gizmoOriginScreen{};
            if (hasGizmoTarget) {
                TransformComponent selectedWorld = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                GizmoAxis activeAxisForThisPane = (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen != screen)
                    ? GizmoAxis::None : ctx.DraggingGizmoAxis;
                hoveredGizmoAxis = DrawAndHitTestGizmo3D(ctx.ActiveGizmoMode, proj, selectedWorld.Translation, activeAxisForThisPane, gizmoOriginScreen);
            }

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
                // buttons doing something distinct, like Unity).
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                    camera3D.Yaw += io.MouseDelta.x * 0.3f;
                    camera3D.Pitch = std::clamp(camera3D.Pitch - io.MouseDelta.y * 0.3f, -89.0f, 89.0f);
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, MeshRendererComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& mesh = candidate.GetComponent<MeshRendererComponent>();
                            float maxScale = std::max({ std::abs(transform.Scale.x), std::abs(transform.Scale.y), std::abs(transform.Scale.z) });
                            float radius = PrimitiveBoundingRadius(mesh.Primitive) * maxScale;

                            glm::vec3 toSphere = transform.Translation - cameraPos;
                            float tClosest = glm::dot(toSphere, rayDir);
                            if (tClosest < 0.0f)
                                continue; // behind the camera
                            glm::vec3 closestPoint = cameraPos + rayDir * tClosest;
                            if (glm::distance(closestPoint, transform.Translation) <= radius && tClosest < closestT) {
                                closestT = tClosest;
                                hit = candidate;
                            }
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
            ctx.Renderer.BeginCustomView(camera.Position, camera.Zoom, static_cast<float>(viewportW), static_cast<float>(viewportH), clearColor);
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
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

            // Collider gizmos -- Unity/Unreal-style green wireframe outlines,
            // drawn for every entity with a collider (not just the selected
            // one), always on top of the rendered sprites but under the
            // translate/rotate/scale gizmo below. Purely an editor-only
            // overlay (ImDrawList, like the translate/rotate/scale gizmo) --
            // colliders are never actually rendered by the real IRenderer2D
            // pass, on desktop or on-device, same as Unity's own Gizmos only
            // ever showing in the Scene view.
            ImDrawList* colliderDrawList = ImGui::GetWindowDrawList();
            const ImU32 colliderColor = IM_COL32(60, 230, 90, 255);
            // Offset is added in world space untransformed by the parent's rotation --
            // a tiny, purely-visual imprecision for rotated parents (colliders are never
            // simulated as children of a moving parent anyway, see Scene::OnRuntimeStart).
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, BoxCollider2DComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& collider = ctx.SceneRef.Registry().get<BoxCollider2DComponent>(handle);
                glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                ImVec2 topLeft = worldToPaneScreen(center - collider.Size);
                ImVec2 bottomRight = worldToPaneScreen(center + collider.Size);
                colliderDrawList->AddRect(topLeft, bottomRight, colliderColor, 0.0f, 0, 2.0f);
            }
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CircleCollider2DComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& collider = ctx.SceneRef.Registry().get<CircleCollider2DComponent>(handle);
                glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                ImVec2 screenCenter = worldToPaneScreen(center);
                colliderDrawList->AddCircle(screenCenter, collider.Radius * camera.Zoom, colliderColor, 32, 2.0f);
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
