#include "DualityEditor/SceneGizmo.h"

namespace Duality {

    namespace {
        constexpr float AxisLength = 40.0f;
        constexpr float AxisHitBand = 6.0f;
        constexpr float ArrowHeadSize = 8.0f;
        constexpr float CenterHandleHalfSize = 6.0f;
    }

    GizmoAxis DrawAndHitTestGizmo2D(const ImVec2& origin, GizmoAxis activeAxis) {
        ImVec2 mouse = ImGui::GetIO().MousePos;

        GizmoAxis hovered = GizmoAxis::None;
        if (mouse.x >= origin.x - CenterHandleHalfSize && mouse.x <= origin.x + CenterHandleHalfSize &&
            mouse.y >= origin.y - CenterHandleHalfSize && mouse.y <= origin.y + CenterHandleHalfSize) {
            hovered = GizmoAxis::Both;
        } else if (mouse.y >= origin.y - AxisHitBand && mouse.y <= origin.y + AxisHitBand &&
                   mouse.x >= origin.x && mouse.x <= origin.x + AxisLength + ArrowHeadSize) {
            hovered = GizmoAxis::X;
        } else if (mouse.x >= origin.x - AxisHitBand && mouse.x <= origin.x + AxisHitBand &&
                   mouse.y >= origin.y && mouse.y <= origin.y + AxisLength + ArrowHeadSize) {
            hovered = GizmoAxis::Y;
        }

        GizmoAxis highlight = (activeAxis != GizmoAxis::None) ? activeAxis : hovered;

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImVec2 xEnd(origin.x + AxisLength, origin.y);
        ImU32 xColor = (highlight == GizmoAxis::X) ? IM_COL32(255, 150, 150, 255) : IM_COL32(230, 70, 70, 255);
        drawList->AddLine(origin, xEnd, xColor, 3.0f);
        drawList->AddTriangleFilled(ImVec2(xEnd.x, xEnd.y - 5.0f), ImVec2(xEnd.x, xEnd.y + 5.0f),
                                     ImVec2(xEnd.x + ArrowHeadSize, xEnd.y), xColor);

        // Y+ points down in the drawn gizmo too, matching this engine's
        // Y-down world convention (positive gravity, etc.) -- an "up" arrow
        // here would silently lie about which way increasing Y drags it.
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

}
