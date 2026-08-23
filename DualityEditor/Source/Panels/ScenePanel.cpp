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
    }

    void ScenePanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Scene");

        // Gizmo tool switcher -- Unity/MyGameEngine's Q/W/E/R convention,
        // minus a "None" mode (Translate is always at least available).
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

        ImVec2 avail = ImGui::GetContentRegionAvail();
        int viewportW = std::max(1, static_cast<int>(avail.x));
        int viewportH = std::max(1, static_cast<int>(avail.y));
        ctx.SceneFramebuffer.Resize(static_cast<uint32_t>(viewportW), static_cast<uint32_t>(viewportH));

        // BeginCustomView's projection is already centered on
        // SceneCameraPos/SceneZoom (see OpenGLRenderer2D::BeginCustomView),
        // so DrawQuad calls below use plain world coordinates -- manually
        // re-applying the camera offset/zoom on top of that would
        // double-transform every position, drifting away from the gizmo/
        // selection math below (which already computes a single correct
        // transform) the more the camera pans or zooms from its default.
        ctx.SceneFramebuffer.Bind();
        ctx.Renderer.BeginCustomView(ctx.SceneCameraPos, ctx.SceneZoom, static_cast<float>(viewportW), static_cast<float>(viewportH), { 0.15f, 0.15f, 0.18f, 1.0f });
        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
            auto& transform = ctx.SceneRef.Registry().get<TransformComponent>(handle);
            auto& sprite = ctx.SceneRef.Registry().get<SpriteRendererComponent>(handle);
            glm::vec2 topLeft{ transform.Translation.x - sprite.Size.x * 0.5f, transform.Translation.y - sprite.Size.y * 0.5f };
            uint32_t textureId = ResolveSpriteTexture(ctx.Renderer, GetActiveSpriteTexture(ctx.SceneRef, handle));
            ctx.Renderer.DrawQuad(topLeft, sprite.Size, sprite.Color, transform.Rotation.z, textureId);
        }
        // Cameras have no sprite of their own -- draw a small gizmo marker
        // at each one's position (color-coded by target screen), sized in
        // world units scaled inversely by zoom so it reads as a constant
        // screen size rather than shrinking/growing with zoom.
        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CameraComponent>()) {
            auto& transform = ctx.SceneRef.Registry().get<TransformComponent>(handle);
            auto& camera = ctx.SceneRef.Registry().get<CameraComponent>(handle);
            float markerSize = 14.0f / ctx.SceneZoom;
            glm::vec4 markerColor = (camera.Screen == Screen::Top) ? glm::vec4{ 0.3f, 0.9f, 0.9f, 1.0f } : glm::vec4{ 0.95f, 0.6f, 0.2f, 1.0f };
            ctx.Renderer.DrawQuad({ transform.Translation.x - markerSize * 0.5f, transform.Translation.y - markerSize * 0.5f }, { markerSize, markerSize }, markerColor);
        }
        ctx.Renderer.EndScene();
        ctx.SceneFramebuffer.Unbind();

        ImVec2 imagePos = ImGui::GetCursorScreenPos();
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(ctx.SceneFramebuffer.GetColorAttachment())), avail, ImVec2(0, 1), ImVec2(1, 0));
        bool imageHovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        auto WorldToSceneScreen = [&](const glm::vec2& worldPos) {
            return ImVec2(
                imagePos.x + (worldPos.x - ctx.SceneCameraPos.x) * ctx.SceneZoom + viewportW * 0.5f,
                imagePos.y + (worldPos.y - ctx.SceneCameraPos.y) * ctx.SceneZoom + viewportH * 0.5f);
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
        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, BoxCollider2DComponent>()) {
            auto& transform = ctx.SceneRef.Registry().get<TransformComponent>(handle);
            auto& collider = ctx.SceneRef.Registry().get<BoxCollider2DComponent>(handle);
            glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
            ImVec2 topLeft = WorldToSceneScreen(center - collider.Size);
            ImVec2 bottomRight = WorldToSceneScreen(center + collider.Size);
            colliderDrawList->AddRect(topLeft, bottomRight, colliderColor, 0.0f, 0, 2.0f);
        }
        for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, CircleCollider2DComponent>()) {
            auto& transform = ctx.SceneRef.Registry().get<TransformComponent>(handle);
            auto& collider = ctx.SceneRef.Registry().get<CircleCollider2DComponent>(handle);
            glm::vec2 center{ transform.Translation.x + collider.Offset.x, transform.Translation.y + collider.Offset.y };
            ImVec2 screenCenter = WorldToSceneScreen(center);
            colliderDrawList->AddCircle(screenCenter, collider.Radius * ctx.SceneZoom, colliderColor, 32, 2.0f);
        }

        // Draw+hit-test the gizmo every frame (not just while hovered) so a
        // drag already in progress keeps tracking even if the mouse drifts
        // outside the image mid-drag.
        bool hasGizmoTarget = ctx.Selected && ctx.Selected.HasComponent<TransformComponent>();
        GizmoAxis hoveredGizmoAxis = GizmoAxis::None;
        ImVec2 gizmoOrigin{};
        if (hasGizmoTarget) {
            auto& selectedTransform = ctx.Selected.GetComponent<TransformComponent>();
            gizmoOrigin = WorldToSceneScreen({ selectedTransform.Translation.x, selectedTransform.Translation.y });
            hoveredGizmoAxis = DrawAndHitTestGizmo2D(ctx.ActiveGizmoMode, gizmoOrigin, ctx.DraggingGizmoAxis);
        }

        if (imageHovered) {
            if (io.MouseWheel != 0.0f)
                ctx.SceneZoom = std::clamp(ctx.SceneZoom * (1.0f + io.MouseWheel * 0.1f), 0.1f, 5.0f);

            // Middle-mouse-drag pans, matching MyGameEngine's own Scene
            // view and Unity's convention (right-click is left unbound,
            // same as the reference -- it was only ever repurposed here as
            // an interim stand-in before this fix).
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                ctx.SceneCameraPos.x -= io.MouseDelta.x / ctx.SceneZoom;
                ctx.SceneCameraPos.y -= io.MouseDelta.y / ctx.SceneZoom;
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (hasGizmoTarget && hoveredGizmoAxis != GizmoAxis::None) {
                    ctx.DraggingGizmoAxis = hoveredGizmoAxis;
                } else {
                    glm::vec2 local{ io.MousePos.x - imagePos.x, io.MousePos.y - imagePos.y };
                    glm::vec2 worldPoint{
                        (local.x - viewportW * 0.5f) / ctx.SceneZoom + ctx.SceneCameraPos.x,
                        (local.y - viewportH * 0.5f) / ctx.SceneZoom + ctx.SceneCameraPos.y
                    };

                    Entity hit;
                    for (auto handle : ctx.SceneRef.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                        Entity candidate(handle, &ctx.SceneRef);
                        auto& transform = candidate.GetComponent<TransformComponent>();
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
        // matching how ImGui's own drag widgets behave.
        if (ctx.DraggingGizmoAxis != GizmoAxis::None) {
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
                    glm::vec2 worldDelta{ io.MouseDelta.x / ctx.SceneZoom, io.MouseDelta.y / ctx.SceneZoom };
                    if (ctx.ActiveGizmoMode == GizmoMode::Scale) {
                        if (ctx.DraggingGizmoAxis == GizmoAxis::X || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            selectedTransform.Scale.x += worldDelta.x * ScaleDragSensitivity;
                        if (ctx.DraggingGizmoAxis == GizmoAxis::Y || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            selectedTransform.Scale.y += worldDelta.y * ScaleDragSensitivity;
                    } else {
                        if (ctx.DraggingGizmoAxis == GizmoAxis::X || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            selectedTransform.Translation.x += worldDelta.x;
                        if (ctx.DraggingGizmoAxis == GizmoAxis::Y || ctx.DraggingGizmoAxis == GizmoAxis::Both)
                            selectedTransform.Translation.y += worldDelta.y;
                    }
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                ctx.DraggingGizmoAxis = GizmoAxis::None;
        }

        ImGui::End();
    }

}
