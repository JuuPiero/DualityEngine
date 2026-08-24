#pragma once

namespace Duality {

    // Lives in its own header for the same reason MeshPrimitive/ProjectionType/Screen do:
    // Reflection/Field.h needs this type in its FieldValue variant, but Field.h can't include
    // Components.h (which itself includes Field.h). See UIRectComponent (Scene/Components.h).
    //
    // A 3x3 grid, matching Unity's own RectTransform anchor presets -- which corner/edge of the
    // physical screen a UI element's Offset is measured from. TopLeft/Offset{0,0} sits flush in
    // the screen's own top-left corner; BottomRight/Offset{0,0} sits flush in the bottom-right;
    // MiddleCenter/Offset{0,0} is exactly centered.
    enum class UIAnchor {
        TopLeft, TopCenter, TopRight,
        MiddleLeft, MiddleCenter, MiddleRight,
        BottomLeft, BottomCenter, BottomRight
    };

}
