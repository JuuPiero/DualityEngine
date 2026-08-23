#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Input/KeyCode.h"

namespace Duality {

    // Unity-old-Input-Manager-style input, backed by real per-frame polling
    // done by whichever platform host owns the actual window/hardware
    // (DualityEditor's Window.cpp on desktop via GLFW, DualityPlayer's
    // Main.cpp on device via libctru's hid* functions) -- this class only
    // holds the resulting state and answers queries, it never polls
    // anything itself.
    //
    // GameScripts (where Behaviour subclasses live) can't call this
    // directly on desktop -- it doesn't link DualityEngine's compiled lib,
    // the same reason a Debug.Log-equivalent doesn't reach it either (see
    // ROADMAP.md). Scripts should call the convenience methods inherited
    // from Behaviour instead (Scene wires those up via EngineServices,
    // see Scripting/EngineServices.h) -- this class is for
    // DualityEditor/DualityPlayer's own use, and as what those Behaviour
    // methods forward to internally.
    class Input {
    public:
        static bool GetKey(KeyCode key);
        static bool GetKeyDown(KeyCode key);
        static bool GetKeyUp(KeyCode key);

        // Only "Horizontal"/"Vertical" are recognized -- not a configurable
        // axis-mapping system like Unity's real Input Manager UI, just its
        // two default axes, hardcoded on both ends (see SetAxis below).
        static float GetAxis(const std::string& axisName);

        static bool GetPointerDown();
        static glm::vec2 GetPointerPosition();

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
        static void SetPointer(bool isDown, const glm::vec2& position);
    };

}
