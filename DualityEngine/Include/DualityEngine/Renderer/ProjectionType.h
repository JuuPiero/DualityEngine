#pragma once

namespace Duality {

    // Lives in its own header (not Components.h, where CameraComponent actually uses it) for
    // the same reason Screen.h does: Reflection/Field.h needs this type in its FieldValue
    // variant, but Field.h can't include Components.h (which itself includes Field.h).
    //
    // Orthographic drives a screen's existing 2D sprite pipeline (unchanged); Perspective
    // switches that screen over to the 3D mesh pipeline for the frame -- a screen is always
    // fully one or the other, never both composited together. See CameraComponent.
    enum class ProjectionType {
        Orthographic,
        Perspective
    };

}
