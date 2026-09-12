#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Engine-side input state manager, backed by real per-frame polling
    // done by whichever platform host owns the actual window/hardware
    // (DualityEditor's Window.cpp on desktop via GLFW, DualityPlayer's
    // Main.cpp on device via libctru's hid* functions) -- this class only
    // holds the resulting state and answers queries, it never polls
    // anything itself.
    //
    // GameScripts use the Unity-facing `Duality::Input` in Scripting/Input.h;
    // that API reaches this manager through EngineServices. InputManager is
    // exclusively for platform hosts (Editor/Player) and engine runtime code.
    class InputManager {
    public:
        static bool GetKey(KeyCode key);
        static bool GetKeyDown(KeyCode key);
        static bool GetKeyUp(KeyCode key);

        // Only "Horizontal"/"Vertical" are recognized -- not a configurable
        // axis-mapping system like Unity's real Input Manager UI, just its
        // two default axes, hardcoded on both ends (see SetAxis below).
        static float GetAxis(const std::string& axisName);

        static bool GetPointerDown();
        static bool GetPointerUp();
        // Local pixel coordinates (top-left origin, Y-down) within whichever screen
        // GetPointerScreen() reports -- NOT a single shared coordinate space across both
        // screens (Top is 400x240, Bottom is 320x240; the platform host resolves which one the
        // pointer is actually over before calling SetPointer, see its own comment below).
        static glm::vec2 GetPointerPosition();
        // Which screen GetPointerPosition() is local to. Meaningless (holds whatever the last
        // SetPointer call passed, arbitrarily defaulting to Top) when GetPointerDown() is false
        // and the pointer isn't over either screen -- check GetPointerDown() first, same as
        // Unity's own Input.GetMouseButtonDown-then-read-position convention.
        static Screen GetPointerScreen();

        // --- Platform-host-only setters below. Plain public statics (no
        // friend-class trick), matching this project's existing convention
        // for internal-but-not-strictly-encapsulated APIs like
        // ScriptRegistry::Register -- only Window.cpp/DualityPlayer's
        // Main.cpp should call these. ---

        // Call once per frame, before polling new state -- rolls this
        // frame's state into "previous" so GetKeyDown/GetKeyUp can detect
        // edges once the new state is written.
        static void BeginFrame();

        static void SetKeyState(KeyCode key, bool isDown);
        static void SetAxis(const std::string& axisName, float value);
        // `position` must already be in `screen`'s own local pixel space (top-left origin,
        // Y-down) -- resolving raw window/touch coordinates down to "which screen, and where
        // on it" is the platform host's job (DualityEditor: GamePanel.cpp, since it's the one
        // that knows each screen's on-screen image rect; DualityPlayerDesktop: Main.cpp's own
        // MapWindowPointToScreen; DualityPlayer/3DS: hardware fact, touch is always Bottom).
        static void SetPointer(bool isDown, const glm::vec2& position, Screen screen);
    };

}
