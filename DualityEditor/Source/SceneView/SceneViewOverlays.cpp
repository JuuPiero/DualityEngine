#include "DualityEditor/SceneView/SceneViewOverlays.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEditor/SceneView/SceneViewNavigation.h"

namespace Duality::SceneViewDraw {

        constexpr float OrientationGizmoRadius = 40.0f;
        constexpr float OrientationGizmoMargin = 55.0f;
        constexpr float OrientationGizmoAxisLength = 30.0f;
        constexpr float OrientationGizmoTipRadius = 8.0f;

        // PiaEngine / Unity axis gizmo, fixed in the pane's top-right corner: world X/Y/Z as
        // seen by the current camera. Click an axis cone = animated snap to that orthographic
        // view, click the centre square or the Persp/Iso label = toggle perspective /
        // orthographic, drag it (3 px threshold) = orbit. Returns true while it owns the mouse
        // (hit-tested before the pane's entity picking, so it never also selects underneath).
        bool DrawAndInteractOrientationGizmo3D(SceneViewCamera3D& camera3D, ImVec2 imagePos, int viewportW, bool imageHovered,
            bool& pressed, bool& dragged, ImVec2& pressPos) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 center(imagePos.x + viewportW - OrientationGizmoMargin, imagePos.y + OrientationGizmoMargin);
            drawList->AddCircleFilled(center, OrientationGizmoRadius, IM_COL32(40, 40, 45, 160));

            struct AxisInfo { glm::vec3 Dir; ImU32 Color; const char* Label; float Yaw, Pitch; };
            const float halfPi = glm::half_pi<float>(), pi = glm::pi<float>();
            // (Yaw, Pitch) of the view looking back at the origin from that axis' side.
            const AxisInfo axes[6] = {
                { { 1.0f, 0.0f, 0.0f }, IM_COL32(230, 70, 70, 255), "X", halfPi, 0.0f },
                { { 0.0f, 1.0f, 0.0f }, IM_COL32(80, 200, 100, 255), "Y", 0.0f, -halfPi },
                { { 0.0f, 0.0f, 1.0f }, IM_COL32(80, 140, 230, 255), "Z", 0.0f, 0.0f },
                { { -1.0f, 0.0f, 0.0f }, IM_COL32(230, 70, 70, 255), nullptr, -halfPi, 0.0f },
                { { 0.0f, -1.0f, 0.0f }, IM_COL32(80, 200, 100, 255), nullptr, 0.0f, halfPi },
                { { 0.0f, 0.0f, -1.0f }, IM_COL32(80, 140, 230, 255), nullptr, pi, 0.0f },
            };
            const glm::quat invRot = glm::inverse(camera3D.Rotation);
            struct Drawn { ImVec2 Tip; const AxisInfo* Axis; float Depth; };
            std::vector<Drawn> drawn;
            drawn.reserve(6);
            for (const AxisInfo& axis : axes) {
                const glm::vec3 viewDir = invRot * axis.Dir;
                drawn.push_back({ ImVec2(center.x + viewDir.x * OrientationGizmoAxisLength, center.y - viewDir.y * OrientationGizmoAxisLength), &axis, viewDir.z });
            }
            std::sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) { return a.Depth < b.Depth; });

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            auto distance = [](ImVec2 a, ImVec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); };
            int hoveredIndex = -1; // nearest-drawn (last) tip under the mouse wins
            for (size_t i = 0; i < drawn.size(); i++)
                if (distance(mouse, drawn[i].Tip) <= OrientationGizmoTipRadius + 2.0f)
                    hoveredIndex = static_cast<int>(i);
            const bool overCenter = std::abs(mouse.x - center.x) <= 6.0f && std::abs(mouse.y - center.y) <= 6.0f;
            const char* projectionLabel = camera3D.Orthographic ? "= Iso" : "< Persp"; // ASCII: the editor font has no U+2261
            const ImVec2 labelSize = ImGui::CalcTextSize(projectionLabel);
            const ImVec2 labelPos(center.x - labelSize.x * 0.5f, center.y + OrientationGizmoRadius + 2.0f);
            const bool overLabel = mouse.x >= labelPos.x - 4.0f && mouse.x <= labelPos.x + labelSize.x + 4.0f &&
                mouse.y >= labelPos.y && mouse.y <= labelPos.y + labelSize.y;

            for (size_t i = 0; i < drawn.size(); i++) {
                const Drawn& d = drawn[i];
                const bool isHovered = static_cast<int>(i) == hoveredIndex && !dragged;
                if (d.Axis->Label) {
                    drawList->AddLine(center, d.Tip, d.Axis->Color, 2.0f);
                    drawList->AddCircleFilled(d.Tip, isHovered ? OrientationGizmoTipRadius + 2.0f : OrientationGizmoTipRadius, d.Axis->Color);
                    const ImVec2 textSize = ImGui::CalcTextSize(d.Axis->Label);
                    drawList->AddText(ImVec2(d.Tip.x - textSize.x * 0.5f, d.Tip.y - textSize.y * 0.5f), IM_COL32(20, 20, 20, 255), d.Axis->Label);
                } else {
                    drawList->AddCircleFilled(d.Tip, isHovered ? OrientationGizmoTipRadius - 1.0f : OrientationGizmoTipRadius - 2.0f, IM_COL32(150, 150, 150, 255));
                }
            }
            drawList->AddRectFilled(ImVec2(center.x - 4.0f, center.y - 4.0f), ImVec2(center.x + 4.0f, center.y + 4.0f),
                overCenter ? IM_COL32(255, 255, 255, 255) : IM_COL32(208, 208, 208, 255));
            drawList->AddText(labelPos, overLabel ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255), projectionLabel);

            const bool overGizmo = distance(mouse, center) <= OrientationGizmoRadius || overLabel;
            if (imageHovered && overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt) {
                pressed = true;
                dragged = false;
                pressPos = mouse;
                camera3D.Anim = -1.0f;
            }
            if (!pressed)
                return false;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (!dragged && distance(mouse, pressPos) > 3.0f)
                    dragged = true;
                if (dragged) {
                    // Dragging the gizmo orbits the view around the pivot.
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    camera3D.Yaw -= delta.x * 0.012f;
                    camera3D.Pitch = std::clamp(camera3D.Pitch - delta.y * 0.012f, -halfPi, halfPi);
                    camera3D.SyncRotation();
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                }
                return true;
            }
            // Released: a click (no drag) picks a view.
            if (!dragged) {
                if (overLabel || overCenter)
                    camera3D.Orthographic = !camera3D.Orthographic;
                else if (hoveredIndex >= 0)
                    SceneViewNavigation::SnapView3D(camera3D, drawn[hoveredIndex].Axis->Yaw, drawn[hoveredIndex].Axis->Pitch, true);
            }
            pressed = dragged = false;
            return true;
        }
        // ---- Tools overlay (Unity 2021 / PiaEngine): a floating column of tool buttons in the
        // top-left corner of each pane. Hit-tested right after the pane image (so a click on it
        // never selects underneath) and drawn after every other overlay (DrawToolsOverlay).
        struct ToolsOverlayButton { GizmoMode Mode; const char* Icon; const char* Tooltip; };
        const ToolsOverlayButton kToolsOverlay[] = {
            { GizmoMode::View, EditorIcons::Hand, "Hand Tool (Q): LMB pans the view" },
            { GizmoMode::Translate, EditorIcons::Move, "Move Tool (W)" },
            { GizmoMode::Rotate, EditorIcons::Rotate, "Rotate Tool (E)" },
            { GizmoMode::Scale, EditorIcons::Scale, "Scale Tool (R)" },
            { GizmoMode::Rect, EditorIcons::RectTool, "Rect Tool (T)" },
        };
        constexpr int ToolsOverlayCount = 5;
        constexpr float ToolsOverlayButtonSize = 26.0f;
        ImVec2 s_ToolsOverlayOrigin[2];
        ImVec2 s_PaneImageSize[2]; // for DescribeSceneView (automation)

        ImVec2 ToolsOverlayButtonMin(ImVec2 origin, int index) {
            return ImVec2(origin.x + 8.0f, origin.y + 8.0f + index * (ToolsOverlayButtonSize + 2.0f));
        }

        // Returns true while the mouse is over the overlay (the pane then ignores the mouse).
        bool HandleToolsOverlay(EditorContext& ctx, Screen screen, ImVec2 imagePos, ImVec2 imageSize, bool imageHovered) {
            s_PaneImageSize[screen == Screen::Top ? 0 : 1] = imageSize;
            s_ToolsOverlayOrigin[screen == Screen::Top ? 0 : 1] = imagePos;
            if (!imageHovered)
                return false;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            for (int i = 0; i < ToolsOverlayCount; ++i) {
                const ImVec2 min = ToolsOverlayButtonMin(imagePos, i);
                if (mouse.x < min.x || mouse.y < min.y || mouse.x > min.x + ToolsOverlayButtonSize || mouse.y > min.y + ToolsOverlayButtonSize)
                    continue;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    ctx.ActiveGizmoMode = kToolsOverlay[i].Mode;
                ImGui::SetTooltip("%s", kToolsOverlay[i].Tooltip);
                return true;
            }
            return false;
        }

        void DrawToolsOverlay(EditorContext& ctx, Screen screen) {
            const ImVec2 origin = s_ToolsOverlayOrigin[screen == Screen::Top ? 0 : 1];
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 panelMin(origin.x + 5.0f, origin.y + 5.0f);
            const ImVec2 panelMax(origin.x + 11.0f + ToolsOverlayButtonSize, origin.y + 11.0f + ToolsOverlayCount * (ToolsOverlayButtonSize + 2.0f) - 2.0f);
            drawList->AddRectFilled(panelMin, panelMax, IM_COL32(30, 30, 34, 215), 5.0f);
            for (int i = 0; i < ToolsOverlayCount; ++i) {
                const ImVec2 min = ToolsOverlayButtonMin(origin, i);
                const ImVec2 max(min.x + ToolsOverlayButtonSize, min.y + ToolsOverlayButtonSize);
                const bool active = ctx.ActiveGizmoMode == kToolsOverlay[i].Mode;
                const bool hovered = mouse.x >= min.x && mouse.y >= min.y && mouse.x <= max.x && mouse.y <= max.y;
                if (active || hovered)
                    drawList->AddRectFilled(min, max, active ? EditorColors::Selection() : IM_COL32(70, 70, 78, 255), 4.0f);
                const ImVec2 iconSize = ImGui::CalcTextSize(kToolsOverlay[i].Icon);
                drawList->AddText(ImVec2(min.x + (ToolsOverlayButtonSize - iconSize.x) * 0.5f, min.y + (ToolsOverlayButtonSize - iconSize.y) * 0.5f),
                    IM_COL32(230, 230, 230, 255), kToolsOverlay[i].Icon);
            }
        }

    ImVec2 PaneImageOrigin(Screen screen) { return s_ToolsOverlayOrigin[screen == Screen::Top ? 0 : 1]; }
    ImVec2 PaneImageSize(Screen screen) { return s_PaneImageSize[screen == Screen::Top ? 0 : 1]; }

}
