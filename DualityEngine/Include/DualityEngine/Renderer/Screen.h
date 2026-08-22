#pragma once

namespace Duality {

    // The 3DS has two independent physical screens: a non-touch top screen
    // (400x240) and a touch-capable bottom screen (320x240). This is a
    // hardware concept the renderer, camera and canvas abstractions are all
    // aware of from the start, even though the desktop backend may only ever
    // render one of them to a preview panel.
    enum class Screen {
        Top,
        Bottom
    };

    constexpr int TopScreenWidth = 400;
    constexpr int TopScreenHeight = 240;
    constexpr int BottomScreenWidth = 320;
    constexpr int BottomScreenHeight = 240;

}
