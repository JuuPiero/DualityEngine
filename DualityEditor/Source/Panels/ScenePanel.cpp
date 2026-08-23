#include "DualityEditor/Panels/ScenePanel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

        // Renders and interacts with one screen's edit-camera pane -- called once
        // for Top, once for Bottom (see OnImGuiRender below). Selection
        // (ctx.Selected) and the active gizmo tool (ctx.ActiveGizmoMode) are shared
        // across both panes (one selection, one gizmo, editable from either side);
        // only the camera (pan/zoom) and the actual drag-in-progress are per-pane --
        // see ctx.DraggingGizmoScreen's own comment for why a drag needs to
        // remember which pane started it.
        void DrawScenePane(EditorContext& ctx, const char* label, Screen screen, SceneViewCamera& camera, Framebuffer& framebuffer, ImVec2 paneSize) {
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

            ImGui::Text("%s", label);

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
        DrawScenePane(ctx, "Top Screen", Screen::Top, ctx.TopSceneView, ctx.TopSceneFramebuffer, paneSize);
        ImGui::EndChild();

        ImGui::SameLine(0.0f, PaneSpacing);

        ImGui::BeginChild("BottomScenePane", paneSize, true);
        DrawScenePane(ctx, "Bottom Screen", Screen::Bottom, ctx.BottomSceneView, ctx.BottomSceneFramebuffer, paneSize);
        ImGui::EndChild();

        ImGui::End();
    }

}
