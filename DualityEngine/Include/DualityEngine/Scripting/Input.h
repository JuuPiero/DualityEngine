#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity's Input class -- static queries routed through EngineServices. This is the public
    // gameplay API; GameScripts cannot call InputManager directly across the DLL boundary on
    // desktop. Requires
    // ScriptContext to be bound (Scene does this before every Behaviour callback).
    class Input {
    public:
        static bool GetKey(KeyCode key) {
            const EngineServices* services = ScriptContext::Services();
            return services && services->GetKey(static_cast<int>(key));
        }

        static bool GetKeyDown(KeyCode key) {
            const EngineServices* services = ScriptContext::Services();
            return services && services->GetKeyDown(static_cast<int>(key));
        }

        static bool GetKeyUp(KeyCode key) {
            const EngineServices* services = ScriptContext::Services();
            return services && services->GetKeyUp(static_cast<int>(key));
        }

        static float GetAxis(const std::string& axisName) {
            const EngineServices* services = ScriptContext::Services();
            return services ? services->GetAxis(axisName.c_str()) : 0.0f;
        }

        static bool GetPointerDown() {
            const EngineServices* services = ScriptContext::Services();
            return services && services->GetPointerDown();
        }

        static glm::vec2 GetPointerPosition() {
            const EngineServices* services = ScriptContext::Services();
            if (!services)
                return { 0.0f, 0.0f };
            float x = 0.0f, y = 0.0f;
            services->GetPointerPosition(&x, &y);
            return { x, y };
        }

        // Which screen GetPointerPosition() is local to -- Top (400x240) and Bottom (320x240)
        // share overlapping local pixel ranges, so this is the only way to tell which one a
        // touch/click actually landed on. Defaults to Screen::Top if ScriptContext isn't bound.
        static Screen GetPointerScreen() {
            const EngineServices* services = ScriptContext::Services();
            return services ? static_cast<Screen>(services->GetPointerScreen()) : Screen::Top;
        }
    };

}
