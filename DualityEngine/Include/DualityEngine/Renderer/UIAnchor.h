#pragma once

#include <glm/glm.hpp>

namespace Duality {

    // Lives in its own header for the same reason MeshPrimitive/ProjectionType/Screen do:
    // Reflection/Field.h needs this type in its FieldValue variant, but Field.h can't include
    // Components.h (which itself includes Field.h). See UIRectComponent (Scene/Components.h).
    //
    // A 3x3 grid, matching Unity's own RectTransform anchor presets. No longer stored on
    // UIRectComponent directly (superseded by AnchorMin/AnchorMax/Pivot) -- kept as an
    // editor-side quick-set convenience (UIAnchorPresetToMinMaxPivot) and as the vocabulary
    // legacy scene/.uidoc data was authored in (LegacyUIAnchorToRectTransform).
    enum class UIAnchor {
        TopLeft, TopCenter, TopRight,
        MiddleLeft, MiddleCenter, MiddleRight,
        BottomLeft, BottomCenter, BottomRight
    };

    // Snaps AnchorMin/AnchorMax/Pivot to the given preset, all three to the same normalized
    // point (a point anchor, never a stretch) -- matches Unity's plain (non-Alt-click) anchor
    // preset button behavior. Does not touch AnchoredPosition/SizeDelta.
    void UIAnchorPresetToMinMaxPivot(UIAnchor preset, glm::vec2& outAnchorMin, glm::vec2& outAnchorMax, glm::vec2& outPivot);

    // Converts a legacy UIAnchor + Offset (pixels, measured inward from whichever edge/corner
    // the anchor names) + Size into the equivalent AnchorMin/AnchorMax/Pivot/AnchoredPosition/
    // SizeDelta that resolves to the exact same pixel rect under the new RectTransform-style
    // ResolveUIRect. Used both to load pre-RectTransform scene files without resetting UI
    // positions. It remains useful for Canvas UI anchor-preset buttons in the Inspector,
    // which has always meant this same "offset is a margin from the anchored edge" semantics.
    void LegacyUIAnchorToRectTransform(UIAnchor preset, glm::vec2 offset, glm::vec2 size,
        glm::vec2& outAnchorMin, glm::vec2& outAnchorMax, glm::vec2& outPivot,
        glm::vec2& outAnchoredPosition, glm::vec2& outSizeDelta);

}
