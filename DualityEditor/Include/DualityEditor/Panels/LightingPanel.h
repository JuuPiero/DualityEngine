#pragma once

namespace Duality {

    struct EditorContext;

    // Unity's Window > Rendering > Lighting: the active scene's environment (ambient color /
    // intensity, saved in the scene file), an overview of its lights against the 8-lights-per-
    // object PICA200 limit, the project's shadow technique, and the last Game-view frame's
    // lighting stats. Floating window toggled from the Window menu (IsOpen()).
    class LightingPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

        static bool& IsOpen();
    };

}
