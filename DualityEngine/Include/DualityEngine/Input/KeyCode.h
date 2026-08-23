#pragma once

namespace Duality {

    // One shared enum spanning both platforms' meaningfully-distinct input
    // concepts -- each backend (Window.cpp on desktop, DualityPlayer's
    // Main.cpp on device) only ever reports the subset it can actually
    // detect: desktop leaves every Gamepad*/D-Pad code permanently false,
    // 3DS leaves every keyboard-named code permanently false. A script
    // checking the "wrong" platform's codes just never sees them pressed,
    // rather than hitting missing-symbol/undefined behavior.
    enum class KeyCode {
        // Desktop keyboard.
        W, A, S, D, Up, Down, Left, Right, Space, Enter, Escape,

        // 3DS face/shoulder buttons + D-Pad.
        GamepadA, GamepadB, GamepadX, GamepadY, GamepadL, GamepadR,
        GamepadStart, GamepadSelect,
        GamepadDPadUp, GamepadDPadDown, GamepadDPadLeft, GamepadDPadRight,

        Count // sentinel, not a real key -- sizes Input's internal state array
    };

}
