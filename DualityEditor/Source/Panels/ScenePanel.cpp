#include "DualityEditor/Panels/ScenePanel.h"

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

#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEditor/SceneView/ComponentGizmos.h"
#include "DualityEditor/SceneView/SceneAssetDrop.h"
#include "DualityEditor/SceneView/SceneGizmoDrawing.h"
#include "DualityEditor/SceneView/SceneViewOverlays.h"
#include "DualityEditor/SceneGizmo.h"
#include "DualityEditor/SceneView/SceneViewNavigation.h"
#include "DualityEditor/SceneView/SceneViewState.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/Windows/SpriteAssetUI.h"
#include "DualityEditor/Windows/TilePaletteWindow.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/MeshReference.h"
#include "DualityEngine/Asset/ModelAsset.h"
#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Renderer/DrawHelpers2D.h"
#include "DualityEngine/Renderer/TrailTextLODRendering.h"
#include "DualityEngine/Renderer/UIWidgetRenderer.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"

namespace Duality {

    using namespace SceneViewDraw;

    namespace {
        // Dragging the Scale gizmo's axis handles moves the mouse by tens
        // a fraction of a world unit, but Scale is a unitless multiplier
        // starting at 1.0 -- this converts one to the other so a drag
        // feels proportionate instead of scale exploding after a few
        // pixels of movement.
        constexpr float ScaleDragSensitivity = 1.0f;

        // A handful of "nice" round grid-cell sizes stepped by zoom level, rather than one
        // fixed size (which would look absurdly dense zoomed out or absurdly sparse zoomed in
        // across the 0.1x-5x zoom range) or fully continuous/smoothly-blended LOD (real Unity
        // behavior, but more machinery than this first pass needs).
        float GridCellSize2D(float pixelsPerUnit) {
            if (pixelsPerUnit > 250.0f) return 0.2f;
            if (pixelsPerUnit > 100.0f) return 0.5f;
            if (pixelsPerUnit > 40.0f) return 1.0f;
            return 2.5f;
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

        // 3D analog of DrawAndHitTestGizmo2D (SceneGizmo.cpp) -- lives here rather than there
        // since it needs this pane's own camera projection (Projector3D), which the 2D gizmo
        // (already given pre-computed screen-space coordinates by its caller) has no use for.
        // Draws X/Y/Z axis handles (Translate/Scale) or three rotation rings (Rotate) projected
        // from `originWorld`, and returns whichever handle the mouse is over -- same contract as
        // the 2D version, this never touches a TransformComponent itself.
        GizmoAxis DrawAndHitTestGizmo3D(GizmoMode mode, const Projector3D& proj, const glm::vec3& originWorld, GizmoAxis activeAxis, ImVec2& outOriginScreen, const glm::quat& frame = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
            if (!proj.Project(originWorld, outOriginScreen))
                return GizmoAxis::None;

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 mouse = ImGui::GetIO().MousePos;
            const float axisLength = Gizmo3DWorldAxisLength(proj, originWorld);

            struct AxisInfo { glm::vec3 Dir; ImU32 Color; ImU32 HighlightColor; GizmoAxis Axis; };
            AxisInfo axes[3] = {
                // Unity's handle colors (red / green / blue, yellow while hovered or dragged).
                { frame * glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(219, 62, 29, 255), IM_COL32(255, 210, 60, 255), GizmoAxis::X },
                { frame * glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(139, 220, 0, 255), IM_COL32(255, 210, 60, 255), GizmoAxis::Y },
                { frame * glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(58, 122, 248, 255), IM_COL32(255, 210, 60, 255), GizmoAxis::Z },
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
                        glm::vec3 worldPt = originWorld + (a * std::cos(t) + b * std::sin(t)) * axisLength;
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
                if (!proj.Project(originWorld + info.Dir * axisLength, screenTip))
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

        // ------------------------------------------------------------------------------------
        // Unity-style Scene view interaction shared by the 2D and 3D panes: click / Ctrl-click
        // selection, marquee (rectangle) selection, multi-object handle drags with Pivot/Center
        // and Local/Global handle settings and Ctrl increment snapping, and fly navigation.
        // ------------------------------------------------------------------------------------

        // One drag of a Move/Rotate/Scale handle. Transforms are recomputed every frame from
        // the values captured when the drag started plus the accumulated (optionally snapped)
        // delta, so snapping never drifts and one drag is one Undo step.
        struct GizmoDragState {
            bool Active = false;
            std::vector<Entity> Targets;
            std::vector<TransformComponent> StartLocal;
            std::vector<TransformComponent> StartWorld;
            glm::vec3 Pivot{ 0.0f };
            glm::quat Frame{ 1.0f, 0.0f, 0.0f, 0.0f };
            glm::vec3 RawTranslate{ 0.0f }; // in handle-axis space (3D) or world XY (2D)
            float RawRotate = 0.0f;
            glm::vec3 RawScale{ 0.0f };
        };
        GizmoDragState s_GizmoDrag;

        // Per-pane mouse interaction (pending click / marquee / fly-through), one per screen.
        struct PaneInteraction {
            bool ClickPending = false;
            ImVec2 ClickStart{};
            Entity ClickHit;
            bool Marquee = false;
            // Camera navigation drag in progress (SceneViewNavigation), started by NavButton.
            SceneViewNavigation::Drag3D Nav3D = SceneViewNavigation::Drag3D::None;
            SceneViewNavigation::Drag2D Nav2D = SceneViewNavigation::Drag2D::None;
            int NavButton = 0;
            // Axis gizmo: pressed on it (click = snap / toggle, drag past 3 px = orbit).
            bool AxisGizmoPressed = false;
            bool AxisGizmoDragged = false;
            ImVec2 AxisGizmoPressPos{};
        };
        PaneInteraction s_PaneInteraction[2];
        PaneInteraction& InteractionFor(Screen screen) { return s_PaneInteraction[screen == Screen::Top ? 0 : 1]; }
        SceneViewPaneOptions& PaneOptionsFor(EditorContext& ctx, Screen screen) { return ctx.Session.ScenePaneOptions[screen == Screen::Top ? 0 : 1]; }

        float Snap(float value, float increment, bool enabled) {
            if (!enabled || increment <= 0.0f)
                return value;
            return std::round(value / increment) * increment;
        }

        std::vector<Entity> GizmoTargets(EditorContext& ctx) {
            std::vector<Entity> targets = EditorCommands::TopLevelSelection(ctx);
            // Primary first so Pivot mode and Local space follow the active object.
            auto primary = std::find(targets.begin(), targets.end(), ctx.Selected);
            if (primary != targets.end() && primary != targets.begin())
                std::rotate(targets.begin(), primary, primary + 1);
            return targets;
        }

        // Where the handle is drawn: the active object's pivot, or the selection's center.
        glm::vec3 HandlePosition(EditorContext& ctx, const std::vector<Entity>& targets) {
            if (targets.empty())
                return glm::vec3(0.0f);
            if (ctx.Session.ToolHandlePosition == HandlePosition::Pivot || targets.size() == 1)
                return ctx.SceneRef.GetWorldTransform(targets.front()).Translation;
            glm::vec3 sum(0.0f);
            for (Entity entity : targets)
                sum += ctx.SceneRef.GetWorldTransform(entity).Translation;
            return sum / static_cast<float>(targets.size());
        }

        glm::quat HandleFrame(EditorContext& ctx, const std::vector<Entity>& targets) {
            if (targets.empty() || ctx.Session.ToolHandleSpace == HandleSpace::Global || ctx.ActiveGizmoMode == GizmoMode::Rotate)
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            return EulerDegreesToQuat(ctx.SceneRef.GetWorldTransform(targets.front()).Rotation);
        }

        void BeginGizmoDrag(EditorContext& ctx) {
            s_GizmoDrag = GizmoDragState{};
            s_GizmoDrag.Active = true;
            s_GizmoDrag.Targets = GizmoTargets(ctx);
            for (Entity entity : s_GizmoDrag.Targets) {
                s_GizmoDrag.StartLocal.push_back(entity.GetComponent<TransformComponent>());
                s_GizmoDrag.StartWorld.push_back(ctx.SceneRef.GetWorldTransform(entity));
            }
            s_GizmoDrag.Pivot = HandlePosition(ctx, s_GizmoDrag.Targets);
            s_GizmoDrag.Frame = HandleFrame(ctx, s_GizmoDrag.Targets);
        }

        const char* GizmoUndoLabel(GizmoMode mode) {
            switch (mode) {
                case GizmoMode::Rotate: return "Rotate";
                case GizmoMode::Scale: return "Scale";
                default: return "Move";
            }
        }

        // Rewrites every target from its drag-start transform. `worldOffset`: translation in
        // world space; `rotateAxis`: 0/1/2 = X/Y/Z Euler axis; `scaleDelta`: added to local scale.
        void ApplyGizmoDrag(EditorContext& ctx, const glm::vec3& worldOffset, int rotateAxis, float rotateDegrees, const glm::vec3& scaleDelta) {
            const bool center = ctx.Session.ToolHandlePosition == HandlePosition::Center && s_GizmoDrag.Targets.size() > 1;
            for (size_t i = 0; i < s_GizmoDrag.Targets.size(); ++i) {
                Entity entity = s_GizmoDrag.Targets[i];
                if (!entity)
                    continue;
                TransformComponent local = s_GizmoDrag.StartLocal[i];
                TransformComponent world = s_GizmoDrag.StartWorld[i];
                bool moved = false;
                if (worldOffset != glm::vec3(0.0f)) {
                    world.Translation += worldOffset;
                    moved = true;
                }
                if (rotateAxis >= 0 && rotateDegrees != 0.0f) {
                    local.Rotation[rotateAxis] += rotateDegrees;
                    if (center) {
                        // Center mode: the objects also orbit the selection center.
                        glm::vec3 axis(0.0f);
                        axis[rotateAxis] = 1.0f;
                        const glm::quat spin = glm::angleAxis(glm::radians(rotateDegrees), axis);
                        world.Translation = s_GizmoDrag.Pivot + spin * (world.Translation - s_GizmoDrag.Pivot);
                        moved = true;
                    }
                }
                if (scaleDelta != glm::vec3(0.0f)) {
                    local.Scale = s_GizmoDrag.StartLocal[i].Scale + scaleDelta;
                    if (center) {
                        const glm::vec3 start = s_GizmoDrag.StartLocal[i].Scale;
                        const glm::vec3 factor(start.x != 0.0f ? local.Scale.x / start.x : 1.0f, start.y != 0.0f ? local.Scale.y / start.y : 1.0f, start.z != 0.0f ? local.Scale.z / start.z : 1.0f);
                        world.Translation = s_GizmoDrag.Pivot + (world.Translation - s_GizmoDrag.Pivot) * factor;
                        moved = true;
                    }
                }
                if (moved) {
                    world.Rotation = s_GizmoDrag.StartWorld[i].Rotation;
                    world.Scale = s_GizmoDrag.StartWorld[i].Scale;
                    local.Translation = ctx.SceneRef.WorldToLocal(entity, world).Translation;
                }
                entity.GetComponent<TransformComponent>() = local;
            }
            MarkSceneDirty(ctx, GizmoUndoLabel(ctx.ActiveGizmoMode));
        }

        // Entities a marquee can pick on `screen`: anything drawn or with a gizmo of its own
        // (pure grouping entities are skipped -- Unity's rect selection picks renderers).
        bool IsMarqueeCandidate(EditorContext& ctx, Entity entity, Screen screen) {
            if (!ShouldRenderOnScreen(ctx.SceneRef, entity.Handle(), screen))
                return false;
            return entity.HasComponent<SpriteRendererComponent>() || entity.HasComponent<MeshRendererComponent>() ||
                entity.HasComponent<SkinnedMeshRendererComponent>() || entity.HasComponent<CameraComponent>() ||
                entity.HasComponent<DirectionalLightComponent>() || entity.HasComponent<PointLightComponent>() ||
                entity.HasComponent<SpotLightComponent>() || entity.HasComponent<BoxCollider2DComponent>() ||
                entity.HasComponent<CircleCollider2DComponent>() || entity.HasComponent<BoxCollider3DComponent>() ||
                entity.HasComponent<SphereCollider3DComponent>() || entity.HasComponent<ParticleSystemComponent>() ||
                entity.HasComponent<LineRendererComponent>() || entity.HasComponent<AudioSourceComponent>() ||
                entity.HasComponent<BehaviourComponent>();
        }

        // Shared tail of a pane's mouse handling: a press that did not grab a handle becomes a
        // click (select the hit object; Ctrl toggles, Shift adds; empty space clears) or, once
        // dragged past the threshold, a marquee selecting every candidate whose pivot projects
        // inside the rectangle. `project` maps a world position into pane screen space.
        template<typename ProjectFn>
        void UpdateClickAndMarquee(EditorContext& ctx, Screen screen, ProjectFn project, ImVec2 imageMin, ImVec2 imageMax) {
            PaneInteraction& pane = InteractionFor(screen);
            if (!pane.ClickPending)
                return;
            ImGuiIO& io = ImGui::GetIO();
            const float dx = io.MousePos.x - pane.ClickStart.x, dy = io.MousePos.y - pane.ClickStart.y;
            if (!pane.Marquee && ImGui::IsMouseDown(ImGuiMouseButton_Left) && dx * dx + dy * dy > 16.0f)
                pane.Marquee = true;

            const ImVec2 rectMin(std::max(imageMin.x, std::min(pane.ClickStart.x, io.MousePos.x)), std::max(imageMin.y, std::min(pane.ClickStart.y, io.MousePos.y)));
            const ImVec2 rectMax(std::min(imageMax.x, std::max(pane.ClickStart.x, io.MousePos.x)), std::min(imageMax.y, std::max(pane.ClickStart.y, io.MousePos.y)));
            if (pane.Marquee) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(rectMin, rectMax, IM_COL32(90, 150, 230, 40));
                drawList->AddRect(rectMin, rectMax, IM_COL32(120, 170, 240, 200));
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                return;

            // Released.
            const bool additive = io.KeyCtrl || io.KeyShift;
            if (pane.Marquee) {
                std::vector<Entity> picked = additive ? ctx.SelectedEntities : std::vector<Entity>{};
                for (Entity entity : ctx.SceneRef.GetHierarchyTraversalOrder()) {
                    if (!IsMarqueeCandidate(ctx, entity, screen))
                        continue;
                    ImVec2 point;
                    if (!project(ctx.SceneRef.GetWorldTransform(entity).Translation, point))
                        continue;
                    if (point.x >= rectMin.x && point.x <= rectMax.x && point.y >= rectMin.y && point.y <= rectMax.y &&
                        std::find(picked.begin(), picked.end(), entity) == picked.end())
                        picked.push_back(entity);
                }
                EditorCommands::SetSelection(ctx, picked, picked.empty() ? Entity{} : picked.back());
                ctx.Session.RequestRevealSelection = true;
            } else if (pane.ClickHit) {
                if (io.KeyCtrl)
                    EditorCommands::ToggleSelected(ctx, pane.ClickHit);
                else if (io.KeyShift && !EditorCommands::IsSelected(ctx, pane.ClickHit)) {
                    std::vector<Entity> extended = ctx.SelectedEntities;
                    extended.push_back(pane.ClickHit);
                    EditorCommands::SetSelection(ctx, extended, pane.ClickHit);
                } else
                    EditorCommands::SelectOnly(ctx, pane.ClickHit);
                ctx.Session.RequestRevealSelection = true;
            } else if (!additive) {
                EditorCommands::ClearSelection(ctx);
            }
            pane.ClickPending = false;
            pane.Marquee = false;
            pane.ClickHit = Entity{};
        }

        void BeginPaneClick(Screen screen, Entity hit) {
            PaneInteraction& pane = InteractionFor(screen);
            pane.ClickPending = true;
            pane.Marquee = false;
            pane.ClickStart = ImGui::GetIO().MousePos;
            pane.ClickHit = hit;
        }

        // World-space bounds of the selection (pivots + sprite extents), for Frame Selected.
        bool SelectionBounds(EditorContext& ctx, glm::vec3& outMin, glm::vec3& outMax) {
            bool any = false;
            for (Entity entity : ctx.SelectedEntities) {
                if (!entity || !entity.HasComponent<TransformComponent>())
                    continue;
                const TransformComponent world = ctx.SceneRef.GetWorldTransform(entity);
                glm::vec3 extent(0.5f);
                if (entity.HasComponent<SpriteRendererComponent>()) {
                    const glm::vec2 size = glm::abs(entity.GetComponent<SpriteRendererComponent>().Size * glm::vec2(world.Scale.x, world.Scale.y)) * 0.5f;
                    extent = glm::vec3(size.x, size.y, 0.01f);
                } else if (entity.HasComponent<MeshRendererComponent>()) {
                    const float radius = MeshBoundingRadius(entity) *
                        std::max({ std::abs(world.Scale.x), std::abs(world.Scale.y), std::abs(world.Scale.z) });
                    extent = glm::vec3(radius);
                }
                const glm::vec3 low = world.Translation - extent, high = world.Translation + extent;
                outMin = any ? glm::min(outMin, low) : low;
                outMax = any ? glm::max(outMax, high) : high;
                any = true;
            }
            return any;
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
                    // Roll-free yaw/pitch looking the same way as the camera: a pitch beyond
                    // +-90 deg (e.g. -186) is the same direction as 180-pitch with yaw + 180.
                    float pitchDegrees = std::remainder(transform.Rotation.x, 360.0f);
                    float yawDegrees = transform.Rotation.y;
                    if (pitchDegrees > 90.0f || pitchDegrees < -90.0f) {
                        pitchDegrees = (pitchDegrees > 0.0f ? 180.0f : -180.0f) - pitchDegrees;
                        yawDegrees += 180.0f;
                    }
                    camera3D.Yaw = glm::radians(std::remainder(yawDegrees, 360.0f));
                    camera3D.Pitch = glm::radians(pitchDegrees);
                    camera3D.SyncRotation();
                    // Places this orbit camera at the exact same position/orientation as the
                    // real camera: Target is a point straight ahead of it, so
                    // Target - forward*Distance recovers transform.Translation exactly. The
                    // pivot is where the view ray meets the ground (y = 0) when that is a
                    // sensible distance, else 10 units ahead -- pan / fly / zoom speeds scale
                    // with the pivot distance (PiaEngine), so a far pivot makes them unusable.
                    glm::vec3 seedForward = camera3D.Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
                    camera3D.Distance = 10.0f;
                    if (seedForward.y < -1e-3f) {
                        const float groundHit = -transform.Translation.y / seedForward.y;
                        if (groundHit >= 1.0f && groundHit <= 100.0f)
                            camera3D.Distance = groundHit;
                    }
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
            // Orthographic (axis gizmo "Iso"): parallel projection showing what perspective shows
            // at the pivot; the eye is pulled far back so nothing in front of the pivot clips.
            const bool ortho = camera3D.Orthographic;
            const float orthoHalfHeight = SceneViewNavigation::OrthoHalfHeight(camera3D);
            glm::vec3 cameraPos = camera3D.Target - forward * (ortho ? camera3D.Distance + 1000.0f : camera3D.Distance);
            glm::vec3 cameraRotation = QuatToEulerDegrees(camera3D.Rotation);
            float aspect = static_cast<float>(viewportW) / static_cast<float>(viewportH);
            constexpr float FovDegrees = SceneViewNavigation::FovDegrees, NearPlane = 0.1f;
            const float FarPlane = ortho ? 7000.0f : 5000.0f;

            framebuffer.Bind();
            glm::vec4 clearColor = (screen == Screen::Top) ? glm::vec4{ 0.15f, 0.15f, 0.18f, 1.0f } : glm::vec4{ 0.18f, 0.15f, 0.15f, 1.0f };
            // The Scene pane owns a free editor camera, but its light selection must be exactly
            // the Game renderer's. Build the camera portion locally and delegate directional
            // light policy to SceneRenderer's shared helper.
            RenderView previewView;
            previewView.TargetScreen = screen;
            previewView.Projection = ortho ? ProjectionType::Orthographic : ProjectionType::Perspective;
            previewView.OrthoHalfHeight = orthoHalfHeight;
            previewView.CameraPosition = cameraPos;
            previewView.CameraRotationDegrees = cameraRotation;
            previewView.FovDegrees = FovDegrees;
            previewView.AspectRatio = aspect;
            previewView.NearPlane = NearPlane;
            previewView.FarPlane = FarPlane;
            PopulateSceneLighting(ctx.SceneRef, previewView);
            renderer3D.BeginScene(previewView, clearColor, true);
            // The exact forward pass the Game view uses (RenderMeshPass): same materials, light
            // selection, render-queue sorting, skinned meshes and blob shadows -- so material and
            // light edits preview live here. Inactive entities' rendered content is hidden here
            // too (user-requested: seeing an entity disappear the instant Active is unchecked is
            // the expected feedback); colliders/frustums/gizmos stay visible regardless. No
            // Culling Mask filter: the editor camera sees every layer of this screen.
            MeshPassOptions meshPass;
            meshPass.CameraFilter = nullptr;
            RenderMeshPass(renderer3D, ctx.SceneRef, screen, previewView, meshPass);
            // Scene 3D shows SpriteRenderers as their actual world-space quads: an XY plane
            // positioned, rotated and scaled by the entity Transform. 2D mode is therefore
            // merely a convenient orthographic editing view, not a different representation.
            // Preserve opaque-mesh depth testing, but don't let transparent texels reserve
            // depth across the rectangular bounds of a sprite quad.
            renderer3D.SetDepthWriteEnabled(false);
            // Match Game and Scene 2D exactly: explicit Sort Order is primary and equal-order
            // sprites follow the visible Hierarchy (last sibling is drawn last/on top). An EnTT
            // component view is not an authoring order and may change after a reload.
            const std::vector<Entity> hierarchy = ctx.SceneRef.GetHierarchyTraversalOrder();
            std::unordered_map<entt::entity, std::size_t> hierarchyOrder;
            hierarchyOrder.reserve(hierarchy.size());
            for (std::size_t i = 0; i < hierarchy.size(); ++i)
                hierarchyOrder[hierarchy[i].Handle()] = i;
            struct EditorSpriteDrawItem { entt::entity Handle; int SortOrder; std::size_t HierarchyOrder; };
            std::vector<EditorSpriteDrawItem> sprites;
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                auto& sprite = ctx.SceneRef.Registry().get<SpriteRendererComponent>(handle);
                if (!sprite.Enabled)
                    continue;
                const auto found = hierarchyOrder.find(handle);
                sprites.push_back({ handle, sprite.SortOrder,
                    found == hierarchyOrder.end() ? hierarchy.size() : found->second });
            }
            std::sort(sprites.begin(), sprites.end(), [](const EditorSpriteDrawItem& a, const EditorSpriteDrawItem& b) {
                if (a.SortOrder != b.SortOrder)
                    return a.SortOrder < b.SortOrder;
                return a.HierarchyOrder < b.HierarchyOrder;
            });
            for (const EditorSpriteDrawItem& item : sprites) {
                const entt::entity handle = item.Handle;
                auto& sprite = ctx.SceneRef.Registry().get<SpriteRendererComponent>(handle);
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                const bool isInactive = !ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef));
                glm::vec4 editorColor = sprite.Color;
                if (isInactive)
                    editorColor.a *= 0.35f; // visible/selectable in Scene, absent in Game
                // This is still a SpriteRenderer, not a material texture. Reuse the 2D
                // renderer's top-left-origin texture cache so this world quad has the same
                // orientation as Scene 2D and Game; renderer3D only consumes the GL handle.
                uint32_t textureId = ResolveSpriteTexture(ctx.Renderer, GetActiveSpriteTexture(ctx.SceneRef, handle));
                // MeshPrimitive::Plane is XZ. Rotate it into XY once, then apply the
                // Sprite entity's complete world rotation. Its material is double-sided in
                // the editor, so it remains visible from either side as a Unity sprite does.
                glm::quat quadRotation = EulerDegreesToQuat(transform.Rotation) *
                    glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                MeshDrawCommand spriteCommand;
                spriteCommand.Primitive = MeshPrimitive::Plane;
                spriteCommand.Translation = transform.Translation;
                spriteCommand.RotationDegrees = QuatToEulerDegrees(quadRotation);
                spriteCommand.Scale = { sprite.Size.x * transform.Scale.x, 1.0f, sprite.Size.y * transform.Scale.y };
                spriteCommand.Color = editorColor;
                spriteCommand.TextureId = textureId;
                spriteCommand.AlphaBlend = true;
                spriteCommand.DepthWrite = false;
                renderer3D.DrawMesh(spriteCommand);
            }
            renderer3D.SetDepthWriteEnabled(true);
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
            if (HandleToolsOverlay(ctx, screen, ImGui::GetItemRectMin(), ImGui::GetItemRectSize(), imageHovered))
                imageHovered = false; // the tools overlay owns the mouse
            ImGuiIO& io = ImGui::GetIO();

            // The world drop point is the cursor ray intersecting the SpriteRenderer XY plane
            // at the current Scene-view focus depth. This keeps a dropped sprite in front of
            // the editor camera instead of arbitrarily placing it at the world origin.
            Projector3D proj{ cameraPos, forward, right, up, FovDegrees, aspect, imagePos, viewportW, viewportH, ortho, orthoHalfHeight };
            glm::vec3 dropOrigin, dropRay;
            proj.Ray(io.MousePos, dropOrigin, dropRay);
            glm::vec3 worldDrop = camera3D.Target;
            if (std::abs(dropRay.z) > 1e-4f) {
                const float t = (camera3D.Target.z - dropOrigin.z) / dropRay.z;
                if (t > 0.0f)
                    worldDrop = dropOrigin + dropRay * t;
            }
            const float nativeWidth = screen == Screen::Top ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
            const float nativeHeight = static_cast<float>(screen == Screen::Top ? TopScreenHeight : BottomScreenHeight);
            const glm::vec2 canvasDrop{
                std::clamp((io.MousePos.x - imagePos.x) / static_cast<float>(viewportW) * nativeWidth, 0.0f, nativeWidth),
                std::clamp((io.MousePos.y - imagePos.y) / static_cast<float>(viewportH) * nativeHeight, 0.0f, nativeHeight)
            };
            AcceptSceneAssetDrop(ctx, screen, imagePos, avail, worldDrop, canvasDrop);
            const SceneViewPaneOptions& paneOptions = PaneOptionsFor(ctx, screen);

            // Unity/Cocos-style ground grid, drawn first (before every other overlay below) so
            // it visually reads as "underneath" the camera frustums/colliders/gizmos, even
            // though (like all of them) it's really just a flat screen-space overlay with no
            // true depth test against the rendered mesh scene. Like PiaEngine's: it follows the
            // camera (snapped to its spacing), switches 1 -> 10 unit cells when zoomed out, and
            // hides in an orthographic view looking flat at it.
            if (paneOptions.ShowGrid && SceneViewNavigation::GridVisible3D(camera3D)) {
                const float gridCellSize = SceneViewNavigation::GridCellSize3D(camera3D);
                DrawGrid3D(proj, SceneViewNavigation::GridOrigin3D(camera3D), gridCellSize, gridCellSize * 50.0f);
            }

            if (paneOptions.ShowGizmos) { // pane toolbar Gizmos toggle
            // Camera body + frustum wireframe for every camera entity. The compact body remains
            // legible at a distance; the frustum shows the actual view volume and Fov/Zoom.
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CameraComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                TransformComponent camTransform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                auto& cameraComponent = ctx.SceneRef.Registry().get<CameraComponent>(handle);
                if (!cameraComponent.Enabled || !ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef)))
                    continue;
                glm::vec3 camForward = OrbitForward(camTransform.Rotation.x, camTransform.Rotation.y);
                glm::vec3 camRight = glm::normalize(glm::cross(camForward, glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::vec3 camUp = glm::cross(camRight, camForward);
                float realScreenHeight = (screen == Screen::Top) ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);
                float camOrthoHalfHeight = (realScreenHeight * 0.5f) / cameraComponent.Zoom; // matches SceneRenderer.cpp's own Zoom convention
                ImU32 frustumColor = (cameraComponent.Screen == Screen::Top) ? IM_COL32(80, 230, 230, 200) : IM_COL32(240, 150, 50, 200);
                DrawCameraFrustum(proj, camTransform.Translation, camForward, camRight, camUp, cameraComponent.Projection, cameraComponent.FovDegrees, camOrthoHalfHeight, aspect, cameraComponent.NearPlane, cameraComponent.FarPlane, frustumColor);
                DrawCameraBodyGizmo(proj, camTransform.Translation, camForward, frustumColor);
            }

            // Directional lights are invisible render-time data, so give each active light a
            // scene-only sun and parallel-ray icon. This is an ImGui overlay and therefore
            // cannot leak into Game framebuffers or the 3DS player build.
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, DirectionalLightComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                Entity light(handle, &ctx.SceneRef);
                const auto& component = light.GetComponent<DirectionalLightComponent>();
                if (!component.Enabled || !ctx.SceneRef.IsEffectivelyActive(light))
                    continue;
                DrawDirectionalLightGizmo(proj, ctx.SceneRef.GetWorldTransform(light));
            }
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, PointLightComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                Entity light(handle, &ctx.SceneRef);
                const auto& component = light.GetComponent<PointLightComponent>();
                if (!component.Enabled || !ctx.SceneRef.IsEffectivelyActive(light))
                    continue;
                DrawPointLightGizmo(proj, ctx.SceneRef.GetWorldTransform(light), component, ctx.Selected == light);
            }
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpotLightComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                Entity light(handle, &ctx.SceneRef);
                const auto& component = light.GetComponent<SpotLightComponent>();
                if (!component.Enabled || !ctx.SceneRef.IsEffectivelyActive(light))
                    continue;
                DrawSpotLightGizmo(proj, ctx.SceneRef.GetWorldTransform(light), component, ctx.Selected == light);
            }

            // 3D collider gizmo -- green wireframe for the selected entity only. Resize handles
            // are always draggable while the entity is selected (no extra Edit toggle needed).
            if (ctx.Selected && ctx.Selected.HasComponent<BoxCollider3DComponent>() && ctx.Selected.GetComponent<BoxCollider3DComponent>().Enabled && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<BoxCollider3DComponent>();
                glm::vec3 center = transform.Translation + collider.Offset;
                glm::quat rotation = EulerDegreesToQuat(transform.Rotation);

                ImVec2 faceHandles[3];
                DrawBoxCollider3D(proj, center, rotation, collider.Size, faceHandles);

                ImVec2 centerScreen;
                if (proj.Project(center, centerScreen)) {
                    const char* ids[3] = { "##ColliderResizeX", "##ColliderResizeY", "##ColliderResizeZ" };
                    for (int i = 0; i < 3; i++) {
                        if (std::isnan(faceHandles[i].x))
                            continue;
                        float newValue = DragCollider3DHandle(ids[i], centerScreen, faceHandles[i], camera3D.Distance, collider.Size[i]);
                        if (newValue != collider.Size[i]) {
                            collider.Size[i] = newValue;
                            MarkSceneDirty(ctx);
                        }
                    }
                }
            }
            if (ctx.Selected && ctx.Selected.HasComponent<SphereCollider3DComponent>() && ctx.Selected.GetComponent<SphereCollider3DComponent>().Enabled && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<SphereCollider3DComponent>();
                glm::vec3 center = transform.Translation + collider.Offset;

                ImVec2 equatorHandle;
                DrawSphereCollider3D(proj, center, collider.Radius, equatorHandle);

                if (!std::isnan(equatorHandle.x)) {
                    ImVec2 centerScreen;
                    if (proj.Project(center, centerScreen)) {
                        float newRadius = DragCollider3DHandle("##ColliderResizeRadius", centerScreen, equatorHandle, camera3D.Distance, collider.Radius);
                        if (newRadius != collider.Radius)
                            collider.Radius = newRadius;
                        MarkSceneDirty(ctx);
                    }
                }
            }
            if (ctx.Selected && ctx.Selected.HasComponent<CapsuleCollider3DComponent>() && ctx.Selected.GetComponent<CapsuleCollider3DComponent>().Enabled && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<CapsuleCollider3DComponent>();
                glm::vec3 center = transform.Translation + collider.Offset;
                glm::quat rotation = EulerDegreesToQuat(transform.Rotation);

                ImVec2 radiusHandle, heightHandle;
                DrawCapsuleCollider3D(proj, center, rotation, collider.Radius, collider.Height, radiusHandle, heightHandle);

                ImVec2 centerScreen;
                if (proj.Project(center, centerScreen)) {
                    bool colliderChanged = false;
                    if (!std::isnan(radiusHandle.x)) {
                        float newRadius = DragCollider3DHandle("##Collider3DCapsuleRadius", centerScreen, radiusHandle, camera3D.Distance, collider.Radius);
                        if (newRadius != collider.Radius) {
                            collider.Radius = newRadius;
                            colliderChanged = true;
                        }
                    }
                    if (!std::isnan(heightHandle.x)) {
                        float newHeight = DragCollider3DHandle("##Collider3DCapsuleHeight", centerScreen, heightHandle, camera3D.Distance, collider.Height);
                        if (newHeight != collider.Height) {
                            collider.Height = newHeight;
                            colliderChanged = true;
                        }
                    }
                    if (colliderChanged)
                        MarkSceneDirty(ctx);
                }
            }

            // Character Controller, 3D joints, Mesh Collider, LOD Group (SceneView/ComponentGizmos.cpp).
            DrawComponentGizmos3D(ctx, screen, ImGui::GetWindowDrawList(),
                [&proj](const glm::vec3& worldPos, ImVec2& out) { return proj.Project(worldPos, out); });
            } // ShowGizmos

            // Draw+hit-test the gizmo every frame (not just while hovered), matching the 2D
            // pane's own reasoning -- a drag already in progress keeps tracking even if the
            // mouse drifts outside the image mid-drag. Any entity with a Transform gets a
            // gizmo when selected (empty GameObjects included), same as the 2D pane.
            // Handles for the whole selection: drawn every frame (not just while hovered) so a
            // drag in progress keeps tracking even if the mouse leaves the image. Pivot/Center
            // picks where the handle sits, Local/Global its orientation (toolbar, Z / X keys).
            const std::vector<Entity> gizmoTargets = GizmoTargets(ctx);
            const bool toolHasHandle = ctx.ActiveGizmoMode != GizmoMode::View;
            bool hasGizmoTarget = paneOptions.ShowGizmos && toolHasHandle && !gizmoTargets.empty() && ShouldRenderOnScreen(ctx.SceneRef, gizmoTargets.front().Handle(), screen);
            GizmoAxis hoveredGizmoAxis = GizmoAxis::None;
            ImVec2 gizmoOriginScreen{};
            const GizmoMode handleMode = ctx.ActiveGizmoMode == GizmoMode::Rect ? GizmoMode::Translate : ctx.ActiveGizmoMode;
            const glm::vec3 handlePosition = (s_GizmoDrag.Active && ctx.DraggingGizmoScreen == screen) ? s_GizmoDrag.Pivot : HandlePosition(ctx, gizmoTargets);
            const glm::quat handleFrame = (s_GizmoDrag.Active && ctx.DraggingGizmoScreen == screen) ? s_GizmoDrag.Frame : HandleFrame(ctx, gizmoTargets);
            if (hasGizmoTarget) {
                GizmoAxis activeAxisForThisPane = (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen != screen)
                    ? GizmoAxis::None : ctx.DraggingGizmoAxis;
                ImGui::PushClipRect(imagePos, ImVec2(imagePos.x + avail.x, imagePos.y + avail.y), true);
                hoveredGizmoAxis = DrawAndHitTestGizmo3D(handleMode, proj, handlePosition, activeAxisForThisPane, gizmoOriginScreen, handleFrame);
                ImGui::PopClipRect();
            }

            // Selection outline (Unity's orange): projected bounding box of each selected mesh /
            // sprite, a small circle for everything else.
            {
                ImDrawList* outline = ImGui::GetWindowDrawList();
                const ImU32 orange = IM_COL32(255, 140, 30, 230);
                for (Entity entity : ctx.SelectedEntities) {
                    if (!entity || !ShouldRenderOnScreen(ctx.SceneRef, entity.Handle(), screen))
                        continue;
                    const TransformComponent world = ctx.SceneRef.GetWorldTransform(entity);
                    glm::vec3 half(0.0f);
                    if (entity.HasComponent<MeshRendererComponent>())
                        half = glm::abs(world.Scale) * 0.5f;
                    else if (entity.HasComponent<SpriteRendererComponent>()) {
                        const glm::vec2 size = entity.GetComponent<SpriteRendererComponent>().Size;
                        half = glm::vec3(std::abs(size.x * world.Scale.x) * 0.5f, std::abs(size.y * world.Scale.y) * 0.5f, 0.0f);
                    }
                    if (half == glm::vec3(0.0f)) {
                        ImVec2 point;
                        if (proj.Project(world.Translation, point))
                            outline->AddCircle(point, 9.0f, orange, 16, 1.5f);
                        continue;
                    }
                    const glm::quat rotation = EulerDegreesToQuat(world.Rotation);
                    ImVec2 corners[8];
                    bool visible = true;
                    for (int c = 0; c < 8 && visible; ++c) {
                        const glm::vec3 local((c & 1) ? half.x : -half.x, (c & 2) ? half.y : -half.y, (c & 4) ? half.z : -half.z);
                        visible = proj.Project(world.Translation + rotation * local, corners[c]);
                    }
                    if (!visible)
                        continue;
                    static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
                    for (const auto& edge : edges)
                        outline->AddLine(corners[edge[0]], corners[edge[1]], orange, 1.5f);
                }
            }

            // Drawn last (on top of everything else in this pane -- a fixed screen-space
            // widget) and hit-tested BEFORE the entity pick below, so clicking it snaps the view
            // instead of also selecting whatever mesh is underneath it.
            PaneInteraction& axisPane = InteractionFor(screen);
            bool orientationGizmoConsumedClick = DrawAndInteractOrientationGizmo3D(camera3D, imagePos, viewportW, imageHovered,
                axisPane.AxisGizmoPressed, axisPane.AxisGizmoDragged, axisPane.AxisGizmoPressPos);
            if (orientationGizmoConsumedClick)
                ctx.Session.SceneViewStateDirty = true;

            // Frame Selected (F / Hierarchy double-click): animated fit of the selection bounds.
            if (ctx.Session.RequestFrameSelected && ctx.Session.FocusedPanel != EditorPanel::Game) {
                glm::vec3 boundsMin, boundsMax;
                if (SelectionBounds(ctx, boundsMin, boundsMax))
                    SceneViewNavigation::Frame3D(camera3D, (boundsMin + boundsMax) * 0.5f, glm::length(boundsMax - boundsMin) * 0.5f);
            }

            // ---- Navigation (PiaEngine / Unity, SceneViewNavigation.h): MMB pan, RMB fly +
            // WASD/QE (Shift x3, wheel = fly speed), Alt+LMB orbit, Alt+RMB dolly, wheel zoom,
            // Hand tool LMB pan. A drag keeps going outside the pane until its button is released.
            PaneInteraction& pane = InteractionFor(screen);
            if (imageHovered && !orientationGizmoConsumedClick && pane.Nav3D == SceneViewNavigation::Drag3D::None) {
                for (int button = 0; button < 3; ++button) {
                    if (!ImGui::IsMouseClicked(button))
                        continue;
                    const auto mode = SceneViewNavigation::PressMode3D(button, io.KeyAlt, ctx.ActiveGizmoMode == GizmoMode::View, false);
                    if (mode != SceneViewNavigation::Drag3D::Select && mode != SceneViewNavigation::Drag3D::None) {
                        pane.Nav3D = mode;
                        pane.NavButton = button;
                    }
                    break;
                }
            }
            if (pane.Nav3D != SceneViewNavigation::Drag3D::None && !ImGui::IsMouseDown(pane.NavButton)) {
                pane.Nav3D = SceneViewNavigation::Drag3D::None;
                ctx.Session.SceneViewStateDirty = true; // saved per scene (SceneViewState.h)
            }
            {
                SceneViewNavigation::PaneInput input;
                input.MousePos = { io.MousePos.x - imagePos.x, io.MousePos.y - imagePos.y };
                input.MouseDelta = { io.MouseDelta.x, io.MouseDelta.y };
                input.ViewportSize = { static_cast<float>(viewportW), static_cast<float>(viewportH) };
                input.Wheel = (imageHovered || pane.Nav3D != SceneViewNavigation::Drag3D::None) ? io.MouseWheel : 0.0f;
                input.DeltaTime = io.DeltaTime;
                input.Alt = io.KeyAlt;
                input.Shift = io.KeyShift;
                if (pane.Nav3D == SceneViewNavigation::Drag3D::Fly) {
                    input.W = ImGui::IsKeyDown(ImGuiKey_W); input.A = ImGui::IsKeyDown(ImGuiKey_A);
                    input.S = ImGui::IsKeyDown(ImGuiKey_S); input.D = ImGui::IsKeyDown(ImGuiKey_D);
                    input.Q = ImGui::IsKeyDown(ImGuiKey_Q); input.E = ImGui::IsKeyDown(ImGuiKey_E);
                    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
                }
                if (SceneViewNavigation::Navigate3D(camera3D, pane.Nav3D, input) && pane.Nav3D == SceneViewNavigation::Drag3D::None)
                    ctx.Session.SceneViewStateDirty = true; // wheel zoom
                if (SceneViewNavigation::Animate3D(camera3D, io.DeltaTime) && camera3D.Anim < 0.0f)
                    ctx.Session.SceneViewStateDirty = true; // animation finished
            }

            if (imageHovered) {
                if (!orientationGizmoConsumedClick && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && ctx.ActiveGizmoMode != GizmoMode::View) {
                    if (hasGizmoTarget && hoveredGizmoAxis != GizmoAxis::None) {
                        ctx.DraggingGizmoAxis = hoveredGizmoAxis;
                        ctx.DraggingGizmoScreen = screen;
                    } else {
                        glm::vec3 rayOrigin, rayDir; // perspective or orthographic (axis gizmo Iso)
                        proj.Ray(io.MousePos, rayOrigin, rayDir);

                        Entity hit;
                        float closestT = std::numeric_limits<float>::max();
                        auto testSphere = [&](Entity candidate, const glm::vec3& center, float radius) {
                            glm::vec3 toSphere = center - rayOrigin;
                            float tClosest = glm::dot(toSphere, rayDir);
                            if (tClosest < 0.0f)
                                return; // behind the camera
                            glm::vec3 closestPoint = rayOrigin + rayDir * tClosest;
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
                            if (!mesh.Enabled || !candidate.HasComponent<MeshFilterComponent>())
                                continue; // draws nothing without a Mesh Filter
                            float maxScale = std::max({ std::abs(transform.Scale.x), std::abs(transform.Scale.y), std::abs(transform.Scale.z) });
                            testSphere(candidate, transform.Translation, MeshBoundingRadius(candidate) * maxScale);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            const auto& sprite = candidate.GetComponent<SpriteRendererComponent>();
                            if (!sprite.Enabled)
                                continue;
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            glm::vec2 size = glm::abs(sprite.Size * glm::vec2(transform.Scale.x, transform.Scale.y));
                            testSphere(candidate, transform.Translation, glm::length(size) * 0.5f);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CameraComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            testSphere(candidate, transform.Translation, 20.0f);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, DirectionalLightComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            const auto& light = candidate.GetComponent<DirectionalLightComponent>();
                            if (!light.Enabled || !ctx.SceneRef.IsEffectivelyActive(candidate))
                                continue;
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            testSphere(candidate, transform.Translation, 20.0f);
                        }
                        // Point/Spot lights: pick the icon, never the (possibly huge) range volume.
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent>()) {
                            Entity candidate(handle, &ctx.SceneRef);
                            if (!candidate.HasComponent<PointLightComponent>() && !candidate.HasComponent<SpotLightComponent>())
                                continue;
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen) || !ctx.SceneRef.IsEffectivelyActive(candidate))
                                continue;
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            testSphere(candidate, transform.Translation, std::max(0.25f, Gizmo3DWorldAxisLength(proj, transform.Translation) * 0.15f));
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, BoxCollider3DComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& collider = candidate.GetComponent<BoxCollider3DComponent>();
                            glm::vec3 center = transform.Translation + collider.Offset;
                            float pickRadius = glm::length(collider.Size);
                            testSphere(candidate, center, pickRadius);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SphereCollider3DComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& collider = candidate.GetComponent<SphereCollider3DComponent>();
                            testSphere(candidate, transform.Translation + collider.Offset, collider.Radius);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CapsuleCollider3DComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& collider = candidate.GetComponent<CapsuleCollider3DComponent>();
                            glm::vec3 center = transform.Translation + collider.Offset;
                            float pickRadius = std::max(collider.Radius, collider.Height * 0.5f);
                            testSphere(candidate, center, pickRadius);
                        }
                        constexpr float EmptyEntityPickRadius3D = 15.0f;
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent>()) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                                continue;
                            Entity candidate(handle, &ctx.SceneRef);
                            if (candidate.HasComponent<MeshRendererComponent>() ||
                                candidate.HasComponent<SpriteRendererComponent>() ||
                                candidate.HasComponent<CameraComponent>() ||
                                candidate.HasComponent<DirectionalLightComponent>() ||
                                candidate.HasComponent<PointLightComponent>() ||
                                candidate.HasComponent<SpotLightComponent>() ||
                                candidate.HasComponent<BoxCollider3DComponent>() ||
                                candidate.HasComponent<SphereCollider3DComponent>() ||
                                candidate.HasComponent<CapsuleCollider3DComponent>())
                                continue;
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            testSphere(candidate, transform.Translation, EmptyEntityPickRadius3D);
                        }
                        BeginPaneClick(screen, hit);
                    }
                }
            }

            // Marquee / click selection (also completes when the mouse is released outside).
            UpdateClickAndMarquee(ctx, screen, [&](const glm::vec3& world, ImVec2& out) { return proj.Project(world, out); },
                imagePos, ImVec2(imagePos.x + avail.x, imagePos.y + avail.y));

            // Handle drag: not gated on imageHovered -- once it starts it follows the mouse even
            // outside the image. Values are recomputed from the drag-start transforms + the
            // accumulated delta (Ctrl = increment snapping, Unity).
            if (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen == screen) {
                if (!s_GizmoDrag.Active)
                    BeginGizmoDrag(ctx);
                const bool snap = io.KeyCtrl;
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !s_GizmoDrag.Targets.empty()) {
                    const glm::vec2 mouseDelta{ io.MouseDelta.x, io.MouseDelta.y };
                    const glm::vec3 axisDirs[3] = { s_GizmoDrag.Frame * glm::vec3(1, 0, 0), s_GizmoDrag.Frame * glm::vec3(0, 1, 0), s_GizmoDrag.Frame * glm::vec3(0, 0, 1) };
                    const int axisIndex = ctx.DraggingGizmoAxis == GizmoAxis::X ? 0 : ctx.DraggingGizmoAxis == GizmoAxis::Y ? 1 : ctx.DraggingGizmoAxis == GizmoAxis::Z ? 2 : -1;
                    // Mouse movement projected onto an axis' screen direction.
                    auto axisScreenDelta = [&](const glm::vec3& axisDir) -> float {
                        ImVec2 originScreen, tipScreen;
                        const glm::vec3 origin = s_GizmoDrag.Pivot;
                        const float axisLength = Gizmo3DWorldAxisLength(proj, origin);
                        if (!proj.Project(origin, originScreen) || !proj.Project(origin + axisDir * axisLength, tipScreen))
                            return 0.0f;
                        glm::vec2 axis2D{ tipScreen.x - originScreen.x, tipScreen.y - originScreen.y };
                        const float length = glm::length(axis2D);
                        return length < 1e-4f ? 0.0f : glm::dot(mouseDelta, axis2D / length) * (axisLength / length);
                    };

                    if (handleMode == GizmoMode::Rotate) {
                        ImVec2 originScreen;
                        float deltaDegrees = 0.0f;
                        if (proj.Project(s_GizmoDrag.Pivot, originScreen)) {
                            ImVec2 prevMouse{ io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y };
                            float angleNow = std::atan2(io.MousePos.y - originScreen.y, io.MousePos.x - originScreen.x);
                            float anglePrev = std::atan2(prevMouse.y - originScreen.y, prevMouse.x - originScreen.x);
                            deltaDegrees = glm::degrees(angleNow - anglePrev);
                            while (deltaDegrees > 180.0f) deltaDegrees -= 360.0f;
                            while (deltaDegrees < -180.0f) deltaDegrees += 360.0f;
                        }
                        s_GizmoDrag.RawRotate += deltaDegrees;
                        ApplyGizmoDrag(ctx, glm::vec3(0.0f), axisIndex, Snap(s_GizmoDrag.RawRotate, ctx.Session.SnapRotate, snap), glm::vec3(0.0f));
                    } else if (handleMode == GizmoMode::Scale) {
                        if (axisIndex >= 0)
                            s_GizmoDrag.RawScale[axisIndex] += axisScreenDelta(axisDirs[axisIndex]) / std::max(0.001f, Gizmo3DWorldAxisLength(proj, s_GizmoDrag.Pivot)) * ScaleDragSensitivity;
                        else
                            s_GizmoDrag.RawScale += glm::vec3((mouseDelta.x - mouseDelta.y) * 0.01f * ScaleDragSensitivity);
                        glm::vec3 effective(Snap(s_GizmoDrag.RawScale.x, ctx.Session.SnapScale, snap), Snap(s_GizmoDrag.RawScale.y, ctx.Session.SnapScale, snap), Snap(s_GizmoDrag.RawScale.z, ctx.Session.SnapScale, snap));
                        ApplyGizmoDrag(ctx, glm::vec3(0.0f), -1, 0.0f, effective);
                    } else {
                        glm::vec3 worldOffset(0.0f);
                        if (axisIndex >= 0) {
                            s_GizmoDrag.RawTranslate[axisIndex] += axisScreenDelta(axisDirs[axisIndex]);
                            worldOffset = axisDirs[axisIndex] * Snap(s_GizmoDrag.RawTranslate[axisIndex], ctx.Session.SnapMove, snap);
                        } else {
                            // Free move in the camera plane.
                            const float worldPerPixel = std::max(0.0001f, glm::dot(s_GizmoDrag.Pivot - cameraPos, forward)) * 2.0f *
                                std::tan(glm::radians(FovDegrees) * 0.5f) / static_cast<float>(viewportH);
                            s_GizmoDrag.RawTranslate += right * mouseDelta.x * worldPerPixel - up * mouseDelta.y * worldPerPixel;
                            worldOffset = glm::vec3(Snap(s_GizmoDrag.RawTranslate.x, ctx.Session.SnapMove, snap),
                                Snap(s_GizmoDrag.RawTranslate.y, ctx.Session.SnapMove, snap), Snap(s_GizmoDrag.RawTranslate.z, ctx.Session.SnapMove, snap));
                        }
                        ApplyGizmoDrag(ctx, worldOffset, -1, 0.0f, glm::vec3(0.0f));
                    }
                }
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ctx.DraggingGizmoAxis = GizmoAxis::None;
                    s_GizmoDrag = GizmoDragState{};
                }
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
                    camera.Position = { defaultWidth * 0.5f / PhysicsUnits::PPU(), defaultHeight * 0.5f / PhysicsUnits::PPU() };
                    camera.Zoom = 1.0f;
                }
                camera.Seeded = true;
            }


            ImVec2 avail = ImGui::GetContentRegionAvail();
            int viewportW = std::max(1, static_cast<int>(avail.x));
            int viewportH = std::max(1, static_cast<int>(avail.y));
            framebuffer.Resize(static_cast<uint32_t>(viewportW), static_cast<uint32_t>(viewportH));

            // BeginCustomView's projection is already centered on camera.Position/
            // worldToPixels, so DrawQuad calls below use plain world coordinates --
            // manually re-applying the camera offset/zoom on top of that would
            // double-transform every position, drifting away from the gizmo/
            // selection math below (which already computes a single correct
            // transform) the more the camera pans or zooms from its default.
            framebuffer.Bind();
            glm::vec4 clearColor = (screen == Screen::Top) ? glm::vec4{ 0.15f, 0.15f, 0.18f, 1.0f } : glm::vec4{ 0.18f, 0.15f, 0.15f, 1.0f };
            const float worldToPixels = camera.Zoom * PhysicsUnits::PPU();

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
                auto& mesh = ctx.SceneRef.Registry().get<MeshRendererComponent>(handle);
                if (!mesh.Enabled)
                    continue;
                const MeshFilterComponent* filter = ctx.SceneRef.Registry().try_get<MeshFilterComponent>(handle);
                MeshPrimitive primitive;
                uint32_t meshHandle = 0;
                if (!filter || !ResolveMeshFilter(ctx.Renderer3D, filter->Mesh, primitive, meshHandle))
                    continue;
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                uint32_t subMeshCount = ctx.Renderer3D.GetSubMeshCount(meshHandle);
                for (uint32_t i = 0; i < subMeshCount; i++) {
                    const AssetRef& materialRef = MaterialForSubMesh(mesh.Materials, i);
                    Material material = ResolveMeshMaterial(materialRef);
                    uint32_t textureId = ResolveMeshTexture(ctx.Renderer3D, material.Texture);
                    ctx.Renderer3D.DrawMesh(primitive, meshHandle, i, transform.Translation, transform.Rotation, transform.Scale, material.Color, textureId);
                }
            }
            ctx.Renderer3D.EndScene();

            ctx.Renderer.BeginCustomView(camera.Position, worldToPixels, static_cast<float>(viewportW), static_cast<float>(viewportH), clearColor, false);
            // Unity/Cocos-style Scene view grid -- drawn via real GL calls (not a screen-space
            // ImDrawList overlay) specifically so it renders BEHIND sprites rather than on top
            // of them; an overlay drawn after ImGui::Image() below would sit on top of the
            // whole already-composited framebuffer, including every opaque sprite.
            ctx.Renderer.DrawGrid(camera.Position, worldToPixels, static_cast<float>(viewportW), static_cast<float>(viewportH), GridCellSize2D(worldToPixels));
            const std::vector<Entity> hierarchy = ctx.SceneRef.GetHierarchyTraversalOrder();
            std::unordered_map<entt::entity, std::size_t> hierarchyOrder;
            hierarchyOrder.reserve(hierarchy.size());
            for (std::size_t i = 0; i < hierarchy.size(); ++i)
                hierarchyOrder[hierarchy[i].Handle()] = i;
            struct EditorSpriteDrawItem { entt::entity Handle; int SortOrder; std::size_t HierarchyOrder; bool IsTilemap; };
            std::vector<EditorSpriteDrawItem> sprites;
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                auto& sprite = ctx.SceneRef.Registry().get<SpriteRendererComponent>(handle);
                if (!sprite.Enabled)
                    continue;
                const auto found = hierarchyOrder.find(handle);
                sprites.push_back({ handle, sprite.SortOrder, found == hierarchyOrder.end() ? hierarchy.size() : found->second, false });
            }
            // Tilemaps sort with sprites exactly like the Game view (SceneRenderer::RenderScreen).
            for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, TilemapComponent>()) {
                if (!ShouldRenderOnScreen(ctx.SceneRef, handle, screen))
                    continue;
                const auto& tilemap = ctx.SceneRef.Registry().get<TilemapComponent>(handle);
                if (!tilemap.Enabled)
                    continue;
                const auto found = hierarchyOrder.find(handle);
                sprites.push_back({ handle, tilemap.SortOrder, found == hierarchyOrder.end() ? hierarchy.size() : found->second, true });
            }
            std::sort(sprites.begin(), sprites.end(), [](const EditorSpriteDrawItem& a, const EditorSpriteDrawItem& b) {
                if (a.SortOrder != b.SortOrder)
                    return a.SortOrder < b.SortOrder;
                if (a.HierarchyOrder != b.HierarchyOrder)
                    return a.HierarchyOrder < b.HierarchyOrder;
                return a.IsTilemap < b.IsTilemap;
            });
            for (const EditorSpriteDrawItem& item : sprites) {
                const entt::entity handle = item.Handle;
                const bool isInactive = !ctx.SceneRef.IsEffectivelyActive(Entity(handle, &ctx.SceneRef));
                if (item.IsTilemap) {
                    // Draw space here is world units (BeginCustomView), so no camera offset.
                    DrawTilemap2D(ctx.Renderer, ctx.SceneRef, handle, glm::vec2(0.0f), 1.0f, glm::vec2(0.0f),
                        glm::vec4(1.0f, 1.0f, 1.0f, isInactive ? 0.35f : 1.0f));
                    continue;
                }
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(Entity(handle, &ctx.SceneRef));
                // Same pivot / sub-sprite / 9-slice / flip rules as the Game view.
                SpriteDrawData drawData;
                if (!BuildSpriteRendererDrawData(ctx.Renderer, ctx.SceneRef, handle, { transform.Translation.x, transform.Translation.y }, 1.0f, drawData))
                    continue;
                if (isInactive)
                    drawData.Color.a *= 0.35f; // Unity-style Scene-only inactive preview
                DrawSprite2D(ctx.Renderer, drawData);
            }
            // Text Mesh (and Trail Renderer during Play) -- this projection is already world units.
            RenderWorldExtras2D(ctx.Renderer, ctx.SceneRef, screen, nullptr, glm::vec2(0.0f), 1.0f, glm::vec2(0.0f));
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
                if (!cameraComponent.Enabled)
                    continue;
                float markerSize = 14.0f / worldToPixels;
                glm::vec4 markerColor = (cameraComponent.Screen == Screen::Top) ? glm::vec4{ 0.3f, 0.9f, 0.9f, 1.0f } : glm::vec4{ 0.95f, 0.6f, 0.2f, 1.0f };
                ctx.Renderer.DrawQuad({ transform.Translation.x - markerSize * 0.5f, transform.Translation.y - markerSize * 0.5f }, { markerSize, markerSize }, markerColor);
            }
            ctx.Renderer.EndScene();
            framebuffer.Unbind();

            ImVec2 imagePos = ImGui::GetCursorScreenPos();
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(framebuffer.GetColorAttachment())), avail, ImVec2(0, 1), ImVec2(1, 0));
            bool imageHovered = ImGui::IsItemHovered();
            if (HandleToolsOverlay(ctx, screen, ImGui::GetItemRectMin(), ImGui::GetItemRectSize(), imageHovered))
                imageHovered = false; // the tools overlay owns the mouse
            ImGuiIO& io = ImGui::GetIO();

            const glm::vec2 worldDrop{
                (io.MousePos.x - imagePos.x - viewportW * 0.5f) / worldToPixels + camera.Position.x,
                (io.MousePos.y - imagePos.y - viewportH * 0.5f) / worldToPixels + camera.Position.y
            };
            const float nativeWidth = screen == Screen::Top ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
            const float nativeHeight = static_cast<float>(screen == Screen::Top ? TopScreenHeight : BottomScreenHeight);
            const glm::vec2 canvasDrop{
                std::clamp((io.MousePos.x - imagePos.x) / static_cast<float>(viewportW) * nativeWidth, 0.0f, nativeWidth),
                std::clamp((io.MousePos.y - imagePos.y) / static_cast<float>(viewportH) * nativeHeight, 0.0f, nativeHeight)
            };
            AcceptSceneAssetDrop(ctx, screen, imagePos, avail, { worldDrop.x, worldDrop.y, 0.0f }, canvasDrop);

            auto worldToPaneScreen = [&](const glm::vec2& worldPos) {
                return ImVec2(
                    imagePos.x + (worldPos.x - camera.Position.x) * worldToPixels + viewportW * 0.5f,
                    imagePos.y + (worldPos.y - camera.Position.y) * worldToPixels + viewportH * 0.5f);
            };

            // Collider gizmo -- green wireframe for the selected entity only. Handles are always
            // draggable while selected (no extra Edit toggle needed).
            ImDrawList* colliderDrawList = ImGui::GetWindowDrawList();
            const ImU32 colliderColor = IM_COL32(60, 230, 90, 255);
            if (PaneOptionsFor(ctx, screen).ShowGizmos) { // pane toolbar Gizmos toggle
            // Offset is added in world space untransformed by the parent's rotation --
            // a tiny, purely-visual imprecision for rotated parents (colliders are never
            // simulated as children of a moving parent anyway, see Scene::OnRuntimeStart).
            if (ctx.Selected && ctx.Selected.HasComponent<BoxCollider2DComponent>() && ctx.Selected.GetComponent<BoxCollider2DComponent>().Enabled && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
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

                float newX = DragCollider2DHandle("##Collider2DResizeX", centerScreen, xHandle, worldToPixels, collider.Size.x);
                if (newX != collider.Size.x) {
                    collider.Size.x = newX;
                    MarkSceneDirty(ctx);
                }
                float newY = DragCollider2DHandle("##Collider2DResizeY", centerScreen, yHandle, worldToPixels, collider.Size.y);
                if (newY != collider.Size.y) {
                    collider.Size.y = newY;
                    MarkSceneDirty(ctx);
                }
            }
            if (ctx.Selected && ctx.Selected.HasComponent<CircleCollider2DComponent>() && ctx.Selected.GetComponent<CircleCollider2DComponent>().Enabled && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<CircleCollider2DComponent>();
                glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                ImVec2 screenCenter = worldToPaneScreen(center);
                colliderDrawList->AddCircle(screenCenter, collider.Radius * worldToPixels, colliderColor, 32, 2.0f);

                ImVec2 radiusHandle = worldToPaneScreen(center + glm::vec2{ collider.Radius, 0.0f });
                colliderDrawList->AddRectFilled(ImVec2(radiusHandle.x - Collider2DHandleHalfSize, radiusHandle.y - Collider2DHandleHalfSize),
                    ImVec2(radiusHandle.x + Collider2DHandleHalfSize, radiusHandle.y + Collider2DHandleHalfSize), colliderColor);

                float newRadius = DragCollider2DHandle("##Collider2DResizeRadius", screenCenter, radiusHandle, worldToPixels, collider.Radius);
                if (newRadius != collider.Radius) {
                    collider.Radius = newRadius;
                    MarkSceneDirty(ctx);
                }
            }
            if (ctx.Selected && ctx.Selected.HasComponent<CapsuleCollider2DComponent>() && ctx.Selected.GetComponent<CapsuleCollider2DComponent>().Enabled && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<CapsuleCollider2DComponent>();
                glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                float halfBody = std::max(0.0f, (collider.Height - 2.0f * collider.Radius) * 0.5f);
                glm::vec2 topCap = center + glm::vec2{ 0.0f, halfBody };
                glm::vec2 bottomCap = center - glm::vec2{ 0.0f, halfBody };
                ImVec2 topCapScreen = worldToPaneScreen(topCap);
                ImVec2 bottomCapScreen = worldToPaneScreen(bottomCap);
                float radiusPx = collider.Radius * worldToPixels;
                DrawCapsule2DOutline(colliderDrawList, topCapScreen, bottomCapScreen, radiusPx, colliderColor);
                colliderDrawList->AddCircleFilled(topCapScreen, radiusPx, IM_COL32(60, 230, 90, 40));
                colliderDrawList->AddCircleFilled(bottomCapScreen, radiusPx, IM_COL32(60, 230, 90, 40));
                if (halfBody > 0.0f) {
                    colliderDrawList->AddRectFilled(
                        ImVec2(topCapScreen.x - radiusPx, topCapScreen.y),
                        ImVec2(bottomCapScreen.x + radiusPx, bottomCapScreen.y),
                        IM_COL32(60, 230, 90, 40));
                }

                ImVec2 centerScreen = worldToPaneScreen(center);
                ImVec2 radiusHandle = worldToPaneScreen(center + glm::vec2{ collider.Radius, 0.0f });
                ImVec2 heightHandle = worldToPaneScreen(topCap + glm::vec2{ 0.0f, collider.Radius });
                colliderDrawList->AddRectFilled(ImVec2(radiusHandle.x - Collider2DHandleHalfSize, radiusHandle.y - Collider2DHandleHalfSize),
                    ImVec2(radiusHandle.x + Collider2DHandleHalfSize, radiusHandle.y + Collider2DHandleHalfSize), colliderColor);
                colliderDrawList->AddRectFilled(ImVec2(heightHandle.x - Collider2DHandleHalfSize, heightHandle.y - Collider2DHandleHalfSize),
                    ImVec2(heightHandle.x + Collider2DHandleHalfSize, heightHandle.y + Collider2DHandleHalfSize), colliderColor);

                float newRadius = DragCollider2DHandle("##Collider2DCapsuleRadius", centerScreen, radiusHandle, worldToPixels, collider.Radius);
                if (newRadius != collider.Radius) {
                    collider.Radius = newRadius;
                    MarkSceneDirty(ctx);
                }
                float newHeight = DragCollider2DHandle("##Collider2DCapsuleHeight", centerScreen, heightHandle, worldToPixels, collider.Height);
                if (newHeight != collider.Height) {
                    collider.Height = newHeight;
                    MarkSceneDirty(ctx);
                }
            }
            if (ctx.Selected && ctx.Selected.HasComponent<PolygonCollider2DComponent>() && ctx.Selected.GetComponent<PolygonCollider2DComponent>().Enabled && ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen)) {
                TransformComponent transform = ctx.SceneRef.GetWorldTransform(ctx.Selected);
                auto& collider = ctx.Selected.GetComponent<PolygonCollider2DComponent>();
                glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                int vertexCount = std::clamp(collider.VertexCount, 3, 8);
                std::vector<ImVec2> polygonScreen;
                polygonScreen.reserve(vertexCount);
                for (int i = 0; i < vertexCount; i++) {
                    glm::vec2 vertex = center + GetPolygonVertex2DConst(collider, i);
                    polygonScreen.push_back(worldToPaneScreen(vertex));
                }
                if (polygonScreen.size() >= 2)
                    colliderDrawList->AddPolyline(polygonScreen.data(), static_cast<int>(polygonScreen.size()), colliderColor, ImDrawFlags_Closed, 2.0f);
            }

            // Edge Collider 2D points, joint anchors, effectors (SceneView/ComponentGizmos.cpp).
            DrawComponentGizmos2D(ctx, screen, colliderDrawList, worldToPaneScreen, worldToPixels);
            } // ShowGizmos

            // Draw+hit-test the gizmo every frame (not just while hovered) so a
            // drag already in progress keeps tracking even if the mouse drifts
            // outside the image mid-drag. Drawn in both panes (the selected
            // entity is shared, editable from whichever pane is more convenient).
            // Selection outline (Unity's orange): each selected sprite's rotated rectangle.
            {
                ImDrawList* outline = ImGui::GetWindowDrawList();
                const ImU32 orange = IM_COL32(255, 140, 30, 230);
                for (Entity entity : ctx.SelectedEntities) {
                    if (!entity || !entity.HasComponent<SpriteRendererComponent>() || !ShouldRenderOnScreen(ctx.SceneRef, entity.Handle(), screen))
                        continue;
                    const TransformComponent world = ctx.SceneRef.GetWorldTransform(entity);
                    const auto& sprite = entity.GetComponent<SpriteRendererComponent>();
                    const glm::vec2 size = sprite.Size * glm::vec2(world.Scale.x, world.Scale.y);
                    const float radians = glm::radians(world.Rotation.z);
                    const float c = std::cos(radians), s = std::sin(radians);
                    ImVec2 corners[4];
                    const glm::vec2 local[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                    const glm::vec2 spritePivot = EffectiveSpritePivot(ctx.SceneRef, entity.Handle()); // sliced sprite's own pivot wins
                    for (int i = 0; i < 4; ++i) {
                        const glm::vec2 offset = (local[i] - spritePivot) * size;
                        corners[i] = worldToPaneScreen({ world.Translation.x + offset.x * c - offset.y * s, world.Translation.y + offset.x * s + offset.y * c });
                    }
                    outline->AddPolyline(corners, 4, orange, ImDrawFlags_Closed, 1.5f);
                }
            }

            // Handles for the whole selection (Pivot / Center, see the toolbar). The View tool
            // has none; the Rect tool moves with the free-move square and resizes with corners.
            const std::vector<Entity> gizmoTargets = GizmoTargets(ctx);
            bool hasGizmoTarget = PaneOptionsFor(ctx, screen).ShowGizmos && ctx.ActiveGizmoMode != GizmoMode::View && !gizmoTargets.empty() &&
                ShouldRenderOnScreen(ctx.SceneRef, gizmoTargets.front().Handle(), screen);
            GizmoAxis hoveredGizmoAxis = GizmoAxis::None;
            ImVec2 gizmoOrigin{};
            const GizmoMode handleMode = ctx.ActiveGizmoMode == GizmoMode::Rect ? GizmoMode::Translate : ctx.ActiveGizmoMode;
            bool rectHandleActive = false;
            if (hasGizmoTarget) {
                const glm::vec3 handleWorld = (s_GizmoDrag.Active && ctx.DraggingGizmoScreen == screen) ? s_GizmoDrag.Pivot : HandlePosition(ctx, gizmoTargets);
                gizmoOrigin = worldToPaneScreen({ handleWorld.x, handleWorld.y });
                // Only the pane that started the current drag reports a hovered handle.
                GizmoAxis activeAxisForThisPane = (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen != screen)
                    ? GizmoAxis::None : ctx.DraggingGizmoAxis;
                ImGui::PushClipRect(imagePos, ImVec2(imagePos.x + avail.x, imagePos.y + avail.y), true);
                hoveredGizmoAxis = DrawAndHitTestGizmo2D(handleMode, gizmoOrigin, activeAxisForThisPane);
                ImGui::PopClipRect();

                // Rect tool corners (single sprite): drag a corner, the opposite one stays put.
                Entity primary = gizmoTargets.front();
                if (ctx.ActiveGizmoMode == GizmoMode::Rect && gizmoTargets.size() == 1 && primary.HasComponent<SpriteRendererComponent>()) {
                    auto& sprite = primary.GetComponent<SpriteRendererComponent>();
                    auto& transform = primary.GetComponent<TransformComponent>();
                    const TransformComponent world = ctx.SceneRef.GetWorldTransform(primary);
                    const glm::vec2 worldSize = sprite.Size * glm::vec2(world.Scale.x, world.Scale.y);
                    const glm::vec2 spritePivot = EffectiveSpritePivot(ctx.SceneRef, primary.Handle());
                    const glm::vec2 topLeft{ world.Translation.x - worldSize.x * spritePivot.x, world.Translation.y - worldSize.y * spritePivot.y };
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRect(worldToPaneScreen(topLeft), worldToPaneScreen(topLeft + worldSize), IM_COL32(90, 160, 255, 220), 0.0f, 0, 1.5f);
                    for (int corner = 0; corner < 4; ++corner) {
                        const bool maxX = corner == 1 || corner == 2, maxY = corner >= 2;
                        const ImVec2 point = worldToPaneScreen({ topLeft.x + (maxX ? worldSize.x : 0.0f), topLeft.y + (maxY ? worldSize.y : 0.0f) });
                        ImGui::PushID(corner);
                        ImGui::SetCursorScreenPos(ImVec2(point.x - 5.0f, point.y - 5.0f));
                        ImGui::InvisibleButton("##RectCorner", ImVec2(10.0f, 10.0f));
                        const bool active = ImGui::IsItemActive();
                        rectHandleActive |= active || ImGui::IsItemHovered();
                        drawList->AddRectFilled(ImVec2(point.x - 4.0f, point.y - 4.0f), ImVec2(point.x + 4.0f, point.y + 4.0f),
                            active ? IM_COL32(255, 210, 60, 255) : IM_COL32(90, 160, 255, 255));
                        if (active && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                            const glm::vec2 delta{ io.MouseDelta.x / worldToPixels, io.MouseDelta.y / worldToPixels };
                            glm::vec2 newSize = worldSize;
                            glm::vec2 move{ 0.0f, 0.0f };
                            newSize.x += maxX ? delta.x : -delta.x;
                            newSize.y += maxY ? delta.y : -delta.y;
                            move.x = delta.x * (maxX ? spritePivot.x : 1.0f - spritePivot.x);
                            move.y = delta.y * (maxY ? spritePivot.y : 1.0f - spritePivot.y);
                            auto safeDiv = [](float a, float b) { return std::abs(b) > 1e-6f ? a / b : a; };
                            sprite.Size = { std::max(0.01f, safeDiv(newSize.x, world.Scale.x)), std::max(0.01f, safeDiv(newSize.y, world.Scale.y)) };
                            const glm::vec2 localMove = WorldDeltaToLocal(ctx.SceneRef, primary, move);
                            transform.Translation.x += localMove.x;
                            transform.Translation.y += localMove.y;
                            MarkSceneDirty(ctx, "Resize");
                        }
                        ImGui::PopID();
                    }
                }
            }
            if (rectHandleActive)
                hoveredGizmoAxis = GizmoAxis::None;

            // Frame Selected (F / Hierarchy double-click): animated fit of the selection bounds.
            const glm::vec2 viewportSize{ static_cast<float>(viewportW), static_cast<float>(viewportH) };
            if (ctx.Session.RequestFrameSelected && ctx.Session.FocusedPanel != EditorPanel::Game) {
                glm::vec3 boundsMin, boundsMax;
                if (SelectionBounds(ctx, boundsMin, boundsMax))
                    SceneViewNavigation::Frame2D(camera, { boundsMin.x, boundsMin.y }, { boundsMax.x, boundsMax.y }, viewportSize, PhysicsUnits::PPU());
            }

            // ---- Navigation (PiaEngine 2D view, SceneViewNavigation.h): MMB / RMB / Alt+LMB /
            // Hand tool LMB pan (the world point under the cursor follows the mouse), wheel zooms
            // toward the cursor. A drag keeps going outside the pane until its button is released.
            PaneInteraction& pane = InteractionFor(screen);
            if (imageHovered && pane.Nav2D == SceneViewNavigation::Drag2D::None) {
                for (int button = 0; button < 3; ++button) {
                    if (!ImGui::IsMouseClicked(button))
                        continue;
                    if (SceneViewNavigation::PressMode2D(button, io.KeyAlt, ctx.ActiveGizmoMode == GizmoMode::View) == SceneViewNavigation::Drag2D::Pan) {
                        pane.Nav2D = SceneViewNavigation::Drag2D::Pan;
                        pane.NavButton = button;
                    }
                    break;
                }
            }
            if (pane.Nav2D != SceneViewNavigation::Drag2D::None && !ImGui::IsMouseDown(pane.NavButton)) {
                pane.Nav2D = SceneViewNavigation::Drag2D::None;
                ctx.Session.SceneViewStateDirty = true;
            }
            {
                SceneViewNavigation::PaneInput input;
                input.MousePos = { io.MousePos.x - imagePos.x, io.MousePos.y - imagePos.y };
                input.MouseDelta = { io.MouseDelta.x, io.MouseDelta.y };
                input.ViewportSize = viewportSize;
                input.Wheel = imageHovered ? io.MouseWheel : 0.0f;
                if (SceneViewNavigation::Navigate2D(camera, pane.Nav2D, input, PhysicsUnits::PPU()) && pane.Nav2D == SceneViewNavigation::Drag2D::None)
                    ctx.Session.SceneViewStateDirty = true; // wheel zoom
                if (SceneViewNavigation::Animate2D(camera, io.DeltaTime) && camera.Anim < 0.0f)
                    ctx.Session.SceneViewStateDirty = true;
            }
            const bool panning2D = pane.Nav2D == SceneViewNavigation::Drag2D::Pan;

            // Tile Palette (Unity): while a brush tool is active, the left mouse paints the
            // palette's target Tilemap instead of selecting / dragging gizmos.
            TilePaletteWindow::SceneInput tileInput;
            tileInput.Hovered = imageHovered && !panning2D && !io.KeyAlt;
            tileInput.TargetScreen = screen;
            tileInput.MouseWorld = { (io.MousePos.x - imagePos.x - viewportW * 0.5f) / worldToPixels + camera.Position.x,
                                     (io.MousePos.y - imagePos.y - viewportH * 0.5f) / worldToPixels + camera.Position.y };
            tileInput.WorldToScreen = [&](const glm::vec2& world) { const ImVec2 p = worldToPaneScreen(world); return glm::vec2(p.x, p.y); };
            const bool tilePainting = TilePaletteWindow::HandleSceneInput(ctx, tileInput);

            if (imageHovered && !rectHandleActive && !panning2D && !tilePainting) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && ctx.ActiveGizmoMode != GizmoMode::View) {
                    if (hasGizmoTarget && hoveredGizmoAxis != GizmoAxis::None) {
                        ctx.DraggingGizmoAxis = hoveredGizmoAxis;
                        ctx.DraggingGizmoScreen = screen;
                    } else {
                        glm::vec2 local{ io.MousePos.x - imagePos.x, io.MousePos.y - imagePos.y };
                        glm::vec2 worldPoint{
                            (local.x - viewportW * 0.5f) / worldToPixels + camera.Position.x,
                            (local.y - viewportH * 0.5f) / worldToPixels + camera.Position.y
                        };

                        Entity hit;
                        auto tryPick = [&](Entity candidate) {
                            if (!ShouldRenderOnScreen(ctx.SceneRef, candidate.Handle(), screen))
                                return;
                            hit = candidate; // topmost (last drawn) match wins
                        };

                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& sprite = candidate.GetComponent<SpriteRendererComponent>();
                            if (!sprite.Enabled)
                                continue;
                            glm::vec2 size = sprite.Size * glm::vec2(transform.Scale.x, transform.Scale.y);
                            const glm::vec2 spritePivot = EffectiveSpritePivot(ctx.SceneRef, handle);
                            glm::vec2 topLeft{ transform.Translation.x - size.x * spritePivot.x,
                                transform.Translation.y - size.y * spritePivot.y };
                            glm::vec2 bottomRight = topLeft + size;
                            bool inside = worldPoint.x >= std::min(topLeft.x, bottomRight.x) &&
                                          worldPoint.x <= std::max(topLeft.x, bottomRight.x) &&
                                          worldPoint.y >= std::min(topLeft.y, bottomRight.y) &&
                                          worldPoint.y <= std::max(topLeft.y, bottomRight.y);
                            if (inside)
                                tryPick(candidate);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, BoxCollider2DComponent>()) {
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& collider = candidate.GetComponent<BoxCollider2DComponent>();
                            glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                            bool inside = std::abs(worldPoint.x - center.x) <= collider.Size.x &&
                                          std::abs(worldPoint.y - center.y) <= collider.Size.y;
                            if (inside)
                                tryPick(candidate);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CircleCollider2DComponent>()) {
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& collider = candidate.GetComponent<CircleCollider2DComponent>();
                            glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                            if (glm::distance(worldPoint, center) <= collider.Radius)
                                tryPick(candidate);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CapsuleCollider2DComponent>()) {
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& collider = candidate.GetComponent<CapsuleCollider2DComponent>();
                            glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                            if (PointInCapsule2D(worldPoint, center, collider.Radius, collider.Height))
                                tryPick(candidate);
                        }
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, PolygonCollider2DComponent>()) {
                            Entity candidate(handle, &ctx.SceneRef);
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            auto& collider = candidate.GetComponent<PolygonCollider2DComponent>();
                            glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
                            glm::vec2 localPoint = worldPoint - center;
                            if (PointInPolygon2D(localPoint, collider))
                                tryPick(candidate);
                        }
                        constexpr float EmptyEntityPickRadius2D = 0.12f;
                        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent>()) {
                            Entity candidate(handle, &ctx.SceneRef);
                            if (candidate.HasComponent<SpriteRendererComponent>() ||
                                candidate.HasComponent<BoxCollider2DComponent>() ||
                                candidate.HasComponent<CircleCollider2DComponent>() ||
                                candidate.HasComponent<CapsuleCollider2DComponent>() ||
                                candidate.HasComponent<PolygonCollider2DComponent>())
                                continue;
                            TransformComponent transform = ctx.SceneRef.GetWorldTransform(candidate);
                            glm::vec2 pivot{ transform.Translation.x, transform.Translation.y };
                            if (glm::distance(worldPoint, pivot) <= EmptyEntityPickRadius2D)
                                tryPick(candidate);
                        }
                        BeginPaneClick(screen, hit);
                    }
                }
            }

            // Marquee / click selection (also completes when the mouse is released outside).
            UpdateClickAndMarquee(ctx, screen, [&](const glm::vec3& world, ImVec2& out) { out = worldToPaneScreen({ world.x, world.y }); return true; },
                imagePos, ImVec2(imagePos.x + avail.x, imagePos.y + avail.y));

            // Handle drag: not gated on imageHovered (follows the mouse outside the image), only
            // in the pane that started it. Recomputed from the drag-start transforms + the
            // accumulated delta, so Ctrl increment snapping never drifts.
            if (ctx.DraggingGizmoAxis != GizmoAxis::None && ctx.DraggingGizmoScreen == screen) {
                if (!s_GizmoDrag.Active)
                    BeginGizmoDrag(ctx);
                const bool snap = io.KeyCtrl;
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !s_GizmoDrag.Targets.empty()) {
                    const glm::vec2 worldDelta{ io.MouseDelta.x / worldToPixels, io.MouseDelta.y / worldToPixels };
                    if (handleMode == GizmoMode::Rotate) {
                        const ImVec2 origin = worldToPaneScreen({ s_GizmoDrag.Pivot.x, s_GizmoDrag.Pivot.y });
                        ImVec2 prevMouse{ io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y };
                        float angleNow = std::atan2(io.MousePos.y - origin.y, io.MousePos.x - origin.x);
                        float anglePrev = std::atan2(prevMouse.y - origin.y, prevMouse.x - origin.x);
                        float deltaDegrees = glm::degrees(angleNow - anglePrev);
                        while (deltaDegrees > 180.0f) deltaDegrees -= 360.0f;
                        while (deltaDegrees < -180.0f) deltaDegrees += 360.0f;
                        s_GizmoDrag.RawRotate += deltaDegrees;
                        ApplyGizmoDrag(ctx, glm::vec3(0.0f), 2, Snap(s_GizmoDrag.RawRotate, ctx.Session.SnapRotate, snap), glm::vec3(0.0f));
                    } else if (handleMode == GizmoMode::Scale) {
                        // Scale is measured along the primary object's local axes.
                        const glm::vec2 localDelta = WorldDeltaToLocal(ctx.SceneRef, s_GizmoDrag.Targets.front(), worldDelta);
                        if (ctx.DraggingGizmoAxis == GizmoAxis::X || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            s_GizmoDrag.RawScale.x += localDelta.x * ScaleDragSensitivity;
                        if (ctx.DraggingGizmoAxis == GizmoAxis::Y || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            s_GizmoDrag.RawScale.y += (ctx.DraggingGizmoAxis == GizmoAxis::Both ? localDelta.x : localDelta.y) * ScaleDragSensitivity;
                        ApplyGizmoDrag(ctx, glm::vec3(0.0f), -1, 0.0f, glm::vec3(Snap(s_GizmoDrag.RawScale.x, ctx.Session.SnapScale, snap),
                            Snap(s_GizmoDrag.RawScale.y, ctx.Session.SnapScale, snap), 0.0f));
                    } else {
                        if (ctx.DraggingGizmoAxis == GizmoAxis::X || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            s_GizmoDrag.RawTranslate.x += worldDelta.x;
                        if (ctx.DraggingGizmoAxis == GizmoAxis::Y || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            s_GizmoDrag.RawTranslate.y += worldDelta.y;
                        ApplyGizmoDrag(ctx, glm::vec3(Snap(s_GizmoDrag.RawTranslate.x, ctx.Session.SnapMove, snap),
                            Snap(s_GizmoDrag.RawTranslate.y, ctx.Session.SnapMove, snap), 0.0f), -1, 0.0f, glm::vec3(0.0f));
                    }
                }
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ctx.DraggingGizmoAxis = GizmoAxis::None;
                    s_GizmoDrag = GizmoDragState{};
                }
            }
        }

        // Shared per-pane chrome (label + 2D/3D toggle), then dispatches to
        // DrawScenePane2D or DrawScenePane3D above -- a pane renders through exactly
        // one pipeline at a time, never both composited together (see RenderMode).
        void DrawScenePane(EditorContext& ctx, const char* label, Screen screen, SceneViewCamera& camera, SceneViewCamera3D& camera3D, RenderMode& renderMode, Framebuffer& framebuffer, ImVec2 paneSize) {
            // Pane header: screen name, then Unity's 2D toggle as a 2D | 3D segmented button.
            auto renderModeButton = [&](const char* buttonLabel, RenderMode mode) {
                const bool active = (renderMode == mode);
                ImGui::PushStyleColor(ImGuiCol_Button, active ? ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
                if (ImGui::SmallButton(buttonLabel))
                    renderMode = mode;
                ImGui::PopStyleColor();
            };
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s %s", screen == Screen::Top ? EditorIcons::Image : EditorIcons::Gamepad, label);
            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, ImGui::GetStyle().ItemSpacing.y));
            renderModeButton("2D", RenderMode::Mode2D);
            ImGui::SameLine();
            renderModeButton("3D", RenderMode::Mode3D);
            ImGui::PopStyleVar();
            // PiaEngine-style pane toolbar: Grid / Gizmos toggles, the pipeline badge and a hint.
            SceneViewPaneOptions& options = ctx.Session.ScenePaneOptions[screen == Screen::Top ? 0 : 1];
            auto toggleButton = [&](const char* id, const char* icon, const char* text, bool& value, const char* tooltip) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, value ? ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
                if (ImGui::SmallButton((std::string(icon) + " " + text + "##" + id).c_str()))
                    value = !value;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", tooltip);
            };
            toggleButton("grid", EditorIcons::Grid, "Grid", options.ShowGrid, "Toggle grid");
            toggleButton("gizmos", EditorIcons::Cubes, "Gizmos", options.ShowGizmos, "Toggle gizmos (component icons, colliders, transform handles)");
            const char* controlsHint = renderMode == RenderMode::Mode3D
                ? "RMB+WASD fly \xC2\xB7 Alt+LMB orbit \xC2\xB7 MMB pan \xC2\xB7 Wheel zoom \xC2\xB7 F frame"
                : "MMB/RMB pan \xC2\xB7 Wheel zoom \xC2\xB7 F frame \xC2\xB7 Ctrl snap";
            // Trailing toolbar text only when it fits on the header line (never clipped mid-word).
            auto trailingText = [](const std::string& text, const std::string& shortText = {}) {
                const float lineEnd = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                const float start = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
                const std::string* shown = &text;
                if (start + ImGui::CalcTextSize(text.c_str()).x > lineEnd)
                    shown = &shortText;
                if (shown->empty() || start + ImGui::CalcTextSize(shown->c_str()).x > lineEnd)
                    return;
                ImGui::SameLine();
                ImGui::TextDisabled("%s", shown->c_str());
            };
            ImGui::SameLine();
            ImGui::TextDisabled("%s", renderMode == RenderMode::Mode3D ? "3D \xC2\xB7 citro3d" : "2D \xC2\xB7 citro2d");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", controlsHint);
            trailingText(controlsHint);

            if (renderMode == RenderMode::Mode2D)
                DrawScenePane2D(ctx, screen, camera, framebuffer, paneSize);
            else
                DrawScenePane3D(ctx, screen, camera3D, ctx.Renderer3D, framebuffer, paneSize);
            DrawToolsOverlay(ctx, screen);
            if (renderMode == RenderMode::Mode3D && InteractionFor(screen).Nav3D == SceneViewNavigation::Drag3D::Fly) {
                // Fly speed readout in the image's bottom-left corner while flying (wheel changes it).
                char flyText[48];
                std::snprintf(flyText, sizeof(flyText), "Fly speed x%.2f (wheel)", camera3D.FlySpeed);
                const ImVec2 textSize = ImGui::CalcTextSize(flyText);
                const ImVec2 min(PaneImageOrigin(screen).x + 8.0f, PaneImageOrigin(screen).y + PaneImageSize(screen).y - textSize.y - 14.0f);
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(min, ImVec2(min.x + textSize.x + 12.0f, min.y + textSize.y + 6.0f), IM_COL32(20, 20, 24, 190), 4.0f);
                drawList->AddText(ImVec2(min.x + 6.0f, min.y + 3.0f), IM_COL32(220, 220, 220, 255), flyText);
            }
        }
    }

    void ScenePanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.Session.ShowScene) {
            ctx.Session.RequestFrameSelected = false;
            return;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 2.0f));
        const bool visible = ImGui::Begin("Scene", &ctx.Session.ShowScene);
        ImGui::PopStyleVar();
        if (!visible) {
            ImGui::End();
            ctx.Session.RequestFrameSelected = false;
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ctx.Session.FocusedPanel = EditorPanel::Scene;
        // Per-scene camera persistence (PiaEngine / Unity): restore once per opened scene.
        if (m_ViewStateScene != ctx.ScenePath) {
            m_ViewStateScene = ctx.ScenePath;
            SceneViewState::Load(ctx);
        }
        // A right/middle click into the Scene view focuses it too (so Delete / Ctrl+D / F act
        // on the selection right after clicking there, like Unity).
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
            ImGui::SetWindowFocus();

        // Two side-by-side panes, one per screen, with a draggable splitter between them.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        constexpr float PaneSpacing = 6.0f;
        m_SplitRatio = std::clamp(m_SplitRatio, 0.15f, 0.85f);
        const float usable = std::max(2.0f, avail.x - PaneSpacing);
        ImVec2 topSize{ std::max(1.0f, usable * m_SplitRatio), std::max(1.0f, avail.y) };
        ImVec2 bottomSize{ std::max(1.0f, usable - topSize.x), std::max(1.0f, avail.y) };

        ImGui::BeginChild("TopScenePane", topSize, ImGuiChildFlags_Border);
        DrawScenePane(ctx, "Top Screen", Screen::Top, ctx.TopSceneView, ctx.TopSceneView3D, ctx.TopRenderMode, ctx.TopSceneFramebuffer, topSize);
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##ScenePaneSplitter", ImVec2(PaneSpacing, avail.y));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive() && usable > 1.0f)
            m_SplitRatio += ImGui::GetIO().MouseDelta.x / usable;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_SplitRatio = 0.5f;
        ImGui::SameLine(0.0f, 0.0f);

        ImGui::BeginChild("BottomScenePane", bottomSize, ImGuiChildFlags_Border);
        DrawScenePane(ctx, "Bottom Screen", Screen::Bottom, ctx.BottomSceneView, ctx.BottomSceneView3D, ctx.BottomRenderMode, ctx.BottomSceneFramebuffer, bottomSize);
        ImGui::EndChild();

        ctx.Session.RequestFrameSelected = false;
        if (ctx.Session.SceneViewStateDirty) {
            ctx.Session.SceneViewStateDirty = false;
            SceneViewState::Save(ctx);
        }
        ImGui::End();
    }

}

namespace Duality {

    std::string DescribeSceneView(EditorContext& ctx, Screen screen) {
        const int index = screen == Screen::Top ? 0 : 1;
        const SceneViewCamera& camera = screen == Screen::Top ? ctx.TopSceneView : ctx.BottomSceneView;
        const SceneViewCamera3D& camera3D = screen == Screen::Top ? ctx.TopSceneView3D : ctx.BottomSceneView3D;
        const RenderMode mode = screen == Screen::Top ? ctx.TopRenderMode : ctx.BottomRenderMode;
        const ImVec2 imagePos = PaneImageOrigin(screen);
        const ImVec2 imageSize = PaneImageSize(screen);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const glm::vec2 local{ mouse.x - imagePos.x, mouse.y - imagePos.y };
        const glm::vec2 world2D = SceneViewNavigation::ScreenToWorld2D(camera, local, { imageSize.x, imageSize.y }, PhysicsUnits::PPU());
        const glm::vec3 eye = camera3D.Position();
        char text[1024];
        std::snprintf(text, sizeof(text),
            "{\"mode\":\"%s\",\"image\":[%.1f,%.1f,%.1f,%.1f],\"mouse\":[%.1f,%.1f],"
            "\"center2D\":[%.5f,%.5f],\"zoom2D\":%.5f,\"orthoSize2D\":%.5f,\"worldUnderMouse2D\":[%.5f,%.5f],"
            "\"pivot\":[%.4f,%.4f,%.4f],\"distance\":%.4f,\"yaw\":%.4f,\"pitch\":%.4f,\"eye\":[%.4f,%.4f,%.4f],"
            "\"orthographic\":%s,\"flySpeed\":%.4f,\"animating\":%s,\"tool\":%d,\"grid\":%s,\"gizmos\":%s}",
            mode == RenderMode::Mode3D ? "3D" : "2D", imagePos.x, imagePos.y, imageSize.x, imageSize.y, mouse.x, mouse.y,
            camera.Position.x, camera.Position.y, camera.Zoom, SceneViewNavigation::OrthoSize2D(camera, imageSize.y, PhysicsUnits::PPU()), world2D.x, world2D.y,
            camera3D.Target.x, camera3D.Target.y, camera3D.Target.z, camera3D.Distance, camera3D.Yaw, camera3D.Pitch, eye.x, eye.y, eye.z,
            camera3D.Orthographic ? "true" : "false", camera3D.FlySpeed, (camera3D.Anim >= 0.0f || camera.Anim >= 0.0f) ? "true" : "false",
            static_cast<int>(ctx.ActiveGizmoMode), ctx.Session.ScenePaneOptions[index].ShowGrid ? "true" : "false",
            ctx.Session.ScenePaneOptions[index].ShowGizmos ? "true" : "false");
        return text;
    }

}
