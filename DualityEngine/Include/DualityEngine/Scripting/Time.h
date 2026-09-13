#pragma once

#include <cstdint>

#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity-style frame clock for Behaviour callbacks. The values are valid in
    // OnCreate/OnEnable/OnUpdate/collision/pointer callbacks; outside Play they
    // return zero. Use the OnUpdate argument when only delta time is needed.
    class Time {
    public:
        static float DeltaTime() {
            const EngineServices* services = ScriptContext::Services();
            return services && services->GetDeltaTime ? services->GetDeltaTime() : 0.0f;
        }

        static float ElapsedTime() {
            const EngineServices* services = ScriptContext::Services();
            return services && services->GetElapsedTime ? services->GetElapsedTime() : 0.0f;
        }

        static uint64_t FrameCount() {
            const EngineServices* services = ScriptContext::Services();
            return services && services->GetFrameCount ? static_cast<uint64_t>(services->GetFrameCount()) : 0;
        }

        // Matches Scene's fixed Box2D/Bullet step. FixedUpdate is not exposed
        // yet, but this is useful for timers and physics-aware game code.
        static constexpr float FixedDeltaTime = 1.0f / 60.0f;
    };

}
