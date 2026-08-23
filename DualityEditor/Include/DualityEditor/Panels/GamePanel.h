#pragma once

namespace Duality {

    struct EditorContext;

    // Exactly what the real TopCamera/BottomCamera entities render,
    // stacked to mirror the console's physical layout -- the same
    // RenderScreen pass DualityPlayer uses on-device. Play/Stop and Reload
    // Scripts live here since they control this view's live output;
    // Save/Load/Build for 3DS moved to the menu bar (see MenuBarPanel).
    class GamePanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
