#include "DualityEditor/SceneGizmo.h"

#include <cmath>

namespace Duality {

    namespace {
        constexpr float AxisLength = 40.0f;
        constexpr float AxisHitBand = 6.0f;
        constexpr float ArrowHeadSize = 8.0f;
        constexpr float ScaleHandleHalfSize = 5.0f;
        constexpr float CenterHandleHalfSize = 6.0f;
        constexpr float RotateRadius = 32.0f;
        constexpr float RotateHitBand = 6.0f;

        GizmoAxis HitTestAxisHandles(const ImVec2& mouse, const ImVec2& origin) {
            if (mouse.x >= origin.x - CenterHandleHalfSize && mouse.x <= origin.x + CenterHandleHalfSize &&
                mouse.y >= origin.y - CenterHandleHalfSize && mouse.y <= origin.y + CenterHandleHalfSize) {
                return GizmoAxis::Both;
            }
            if (mouse.y >= origin.y - AxisHitBand && mouse.y <= origin.y + AxisHitBand &&
                mouse.x >= origin.x && mouse.x <= origin.x + AxisLength + ArrowHeadSize) {
                return GizmoAxis::X;
            }
            if (mouse.x >= origin.x - AxisHitBand && mouse.x <= origin.x + AxisHitBand &&
                mouse.y >= origin.y && mouse.y <= origin.y + AxisLength + ArrowHeadSize) {
                return GizmoAxis::Y;
            }
            return GizmoAxis::None;
        }

        GizmoAxis DrawTranslateGizmo(const ImVec2& origin, GizmoAxis activeAxis) {
            ImVec2 mouse = ImGui::GetIO().MousePos;
            GizmoAxis hovered = HitTestAxisHandles(mouse, origin);
            GizmoAxis highlight = (activeAxis != GizmoAxis::None) ? activeAxis : hovered;

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            ImVec2 xEnd(origin.x + AxisLength, origin.y);
            ImU32 xColor = (highlight == GizmoAxis::X) ? IM_COL32(255, 150, 150, 255) : IM_COL32(230, 70, 70, 255);
            drawList->AddLine(origin, xEnd, xColor, 3.0f);
            drawList->AddTriangleFilled(ImVec2(xEnd.x, xEnd.y - 5.0f), ImVec2(xEnd.x, xEnd.y + 5.0f),
                                         ImVec2(xEnd.x + ArrowHeadSize, xEnd.y), xColor);

            // Y+ points down in the drawn gizmo too, matching this engine's
            // Y-down world convention (positive gravity, etc.) -- an "up"
            // arrow here would silently lie about which way increasing Y
            // drags it.
            ImVec2 yEnd(origin.x, origin.y + AxisLength);
            ImU32 yColor = (highlight == GizmoAxis::Y) ? IM_COL32(160, 255, 180, 255) : IM_COL32(80, 200, 100, 255);
            drawList->AddLine(origin, yEnd, yColor, 3.0f);
            drawList->AddTriangleFilled(ImVec2(yEnd.x - 5.0f, yEnd.y), ImVec2(yEnd.x + 5.0f, yEnd.y),
                                         ImVec2(yEnd.x, yEnd.y + ArrowHeadSize), yColor);

            ImU32 centerColor = (highlight == GizmoAxis::Both) ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 210, 70, 255);
            drawList->AddRectFilled(ImVec2(origin.x - CenterHandleHalfSize, origin.y - CenterHandleHalfSize),
                                     ImVec2(origin.x + CenterHandleHalfSize, origin.y + CenterHandleHalfSize), centerColor);

            return hovered;
        }

        GizmoAxis DrawScaleGizmo(const ImVec2& origin, GizmoAxis activeAxis) {
            ImVec2 mouse = ImGui::GetIO().MousePos;
            GizmoAxis hovered = HitTestAxisHandles(mouse, origin);
            GizmoAxis highlight = (activeAxis != GizmoAxis::None) ? activeAxis : hovered;

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // Same axis layout as Translate, but square end-caps instead of
            // arrowheads -- the standard visual distinction between "move"
            // and "scale" handles in Unity-style editors.
            ImVec2 xEnd(origin.x + AxisLength, origin.y);
            ImU32 xColor = (highlight == GizmoAxis::X) ? IM_COL32(255, 150, 150, 255) : IM_COL32(230, 70, 70, 255);
            drawList->AddLine(origin, xEnd, xColor, 3.0f);
            drawList->AddRectFilled(ImVec2(xEnd.x - ScaleHandleHalfSize, xEnd.y - ScaleHandleHalfSize),
                                     ImVec2(xEnd.x + ScaleHandleHalfSize, xEnd.y + ScaleHandleHalfSize), xColor);

            ImVec2 yEnd(origin.x, origin.y + AxisLength);
            ImU32 yColor = (highlight == GizmoAxis::Y) ? IM_COL32(160, 255, 180, 255) : IM_COL32(80, 200, 100, 255);
            drawList->AddLine(origin, yEnd, yColor, 3.0f);
            drawList->AddRectFilled(ImVec2(yEnd.x - ScaleHandleHalfSize, yEnd.y - ScaleHandleHalfSize),
                                     ImVec2(yEnd.x + ScaleHandleHalfSize, yEnd.y + ScaleHandleHalfSize), yColor);

            ImU32 centerColor = (highlight == GizmoAxis::Both) ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 210, 70, 255);
            drawList->AddRectFilled(ImVec2(origin.x - CenterHandleHalfSize, origin.y - CenterHandleHalfSize),
                                     ImVec2(origin.x + CenterHandleHalfSize, origin.y + CenterHandleHalfSize), centerColor);

            return hovered;
        }

        GizmoAxis DrawRotateGizmo(const ImVec2& origin, GizmoAxis activeAxis) {
            ImVec2 mouse = ImGui::GetIO().MousePos;
            float dx = mouse.x - origin.x, dy = mouse.y - origin.y;
            float distance = std::sqrt(dx * dx + dy * dy);
            bool hovered = distance >= RotateRadius - RotateHitBand && distance <= RotateRadius + RotateHitBand;
            bool highlight = hovered || activeAxis != GizmoAxis::None;

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 color = highlight ? IM_COL32(255, 240, 140, 255) : IM_COL32(210, 190, 90, 255);
            drawList->AddCircle(origin, RotateRadius, color, 48, 3.0f);

            return hovered ? GizmoAxis::Both : GizmoAxis::None;
        }
    }

    GizmoAxis DrawAndHitTestGizmo2D(GizmoMode mode, const ImVec2& origin, GizmoAxis activeAxis) {
        switch (mode) {
            case GizmoMode::Rotate: return DrawRotateGizmo(origin, activeAxis);
            case GizmoMode::Scale:  return DrawScaleGizmo(origin, activeAxis);
            default:                return DrawTranslateGizmo(origin, activeAxis);
        }
    }

}
