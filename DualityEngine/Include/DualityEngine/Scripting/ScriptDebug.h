#pragma once

#include <string>

#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity's Debug.Log -- routed through EngineServices so script logs appear in the
    // Editor Console panel exactly like every other engine log line.
    class ScriptDebug {
    public:
        static void LogInfo(const std::string& message) {
            const EngineServices* services = ScriptContext::Services();
            if (services)
                services->LogInfo(message.c_str());
        }

        static void LogWarn(const std::string& message) {
            const EngineServices* services = ScriptContext::Services();
            if (services)
                services->LogWarn(message.c_str());
        }

        static void LogError(const std::string& message) {
            const EngineServices* services = ScriptContext::Services();
            if (services)
                services->LogError(message.c_str());
        }
    };

}
