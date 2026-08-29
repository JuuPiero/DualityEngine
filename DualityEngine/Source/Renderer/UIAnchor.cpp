#include "DualityEngine/Renderer/UIAnchor.h"

namespace Duality {

    // The old 9-way enum is the cross product of an independent X behavior (Left/Center/Right)
    // and Y behavior (Top/Middle/Bottom) -- each maps to a normalized fraction along its axis.
    static void LegacyAnchorFraction(UIAnchor preset, float& outFx, float& outFy) {
        switch (preset) {
            case UIAnchor::TopLeft:      outFx = 0.0f; outFy = 0.0f; break;
            case UIAnchor::TopCenter:    outFx = 0.5f; outFy = 0.0f; break;
            case UIAnchor::TopRight:     outFx = 1.0f; outFy = 0.0f; break;
            case UIAnchor::MiddleLeft:   outFx = 0.0f; outFy = 0.5f; break;
            case UIAnchor::MiddleCenter: outFx = 0.5f; outFy = 0.5f; break;
            case UIAnchor::MiddleRight:  outFx = 1.0f; outFy = 0.5f; break;
            case UIAnchor::BottomLeft:   outFx = 0.0f; outFy = 1.0f; break;
            case UIAnchor::BottomCenter: outFx = 0.5f; outFy = 1.0f; break;
            case UIAnchor::BottomRight:  outFx = 1.0f; outFy = 1.0f; break;
        }
    }

    void UIAnchorPresetToMinMaxPivot(UIAnchor preset, glm::vec2& outAnchorMin, glm::vec2& outAnchorMax, glm::vec2& outPivot) {
        float fx, fy;
        LegacyAnchorFraction(preset, fx, fy);
        outAnchorMin = outAnchorMax = outPivot = { fx, fy };
    }

    void LegacyUIAnchorToRectTransform(UIAnchor preset, glm::vec2 offset, glm::vec2 size,
        glm::vec2& outAnchorMin, glm::vec2& outAnchorMax, glm::vec2& outPivot,
        glm::vec2& outAnchoredPosition, glm::vec2& outSizeDelta) {
        float fx, fy;
        LegacyAnchorFraction(preset, fx, fy);
        outAnchorMin = outAnchorMax = outPivot = { fx, fy };
        // Old Offset was always a margin measured inward from whichever edge the anchor names --
        // the right/bottom edge (fraction 1) needs the sign flipped to become a raw signed
        // AnchoredPosition; left/top/center (fraction 0 or 0.5) already match the sign.
        outAnchoredPosition.x = (fx == 1.0f) ? -offset.x : offset.x;
        outAnchoredPosition.y = (fy == 1.0f) ? -offset.y : offset.y;
        outSizeDelta = size;
    }

}
