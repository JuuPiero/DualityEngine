#pragma once

namespace Duality {

    struct EditorContext;

    // Reflection-driven field editor for every component on the selected
    // entity (unchanged from before this panel was extracted), plus:
    // "..." per non-mandatory component header opens a small popup to
    // remove it (Transform/Name/Tag have no such option -- they're
    // mandatory), and a "+ Add Component" popup at the bottom lists every
    // registered component type not already present.
    class PropertiesPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
