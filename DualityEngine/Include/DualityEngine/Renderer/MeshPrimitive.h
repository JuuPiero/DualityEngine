#pragma once

namespace Duality {

    // Lives in its own header (not Components.h, where MeshRendererComponent actually uses
    // it) for the same reason Screen.h does: Reflection/Field.h needs this type in its
    // FieldValue variant, but Field.h can't include Components.h (which itself includes
    // Field.h). See MeshRendererComponent's own comment for what this models.
    enum class MeshPrimitive {
        Cube,
        Sphere,
        Plane
    };

}
