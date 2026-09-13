#pragma once

#include <string>

#include "DualityEngine/Scripting/ScriptDebug.h"

namespace Duality {

    // Unity-style name for script diagnostics. Assert is deliberately
    // non-fatal: native C++ assertions abort the process on 3DS, while this
    // logs into the Editor Console and lets a script fail gracefully instead.
    class Debug {
    public:
        static void Log(const std::string& message) { ScriptDebug::LogInfo(message); }
        static void LogWarning(const std::string& message) { ScriptDebug::LogWarn(message); }
        static void LogError(const std::string& message) { ScriptDebug::LogError(message); }
        static bool Assert(bool condition, const std::string& message) {
            if (!condition)
                LogError("Assertion failed: " + message);
            return condition;
        }
    };

}
