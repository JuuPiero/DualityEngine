#pragma once

namespace Duality {

    // Unlit preserves the legacy Color + Texture output. VertexLit is the first
    // portable forward-lighting tier: ambient plus a directional Lambert term.
    enum class MaterialShadingMode {
        Unlit,
        VertexLit
    };

}
