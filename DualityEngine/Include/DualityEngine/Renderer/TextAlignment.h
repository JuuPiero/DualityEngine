#pragma once

namespace Duality {

    // Lives in its own header for the same reason UIAnchor/MeshPrimitive/CanvasRenderMode do:
    // Reflection/Field.h needs this type in its FieldValue variant, but Field.h can't include
    // Components.h (which itself includes Field.h). See UITextComponent (Scene/Components.h).
    //
    // Horizontal-only -- vertical text alignment within a UIRectComponent's rect is always
    // centered (a deliberate v1 scope limit, see UITextComponent's own comment).
    enum class TextAlignment {
        Left, Center, Right
    };

}
