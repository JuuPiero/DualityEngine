#pragma once

#include "DualityEngine/Scripting/EngineServices.h"

namespace Duality {

    // Per-callback execution context for Unity-style static scripting APIs
    // (Input, ScriptPhysics2D/3D, ScriptDebug, ScriptAudio, ScriptScene).
    // Scene binds this right before every Behaviour lifecycle/collision callback
    // and clears it afterward -- scripts never call Bind/Clear themselves.
    //
    // s_Services itself is still a plain inline static (and therefore still duplicated per
    // binary on Windows/MinGW, same as everything else header-only) -- but EnsureBound below
    // self-heals THIS binary's own copy to the correct, valid, always-the-same-address value
    // every single time any Behaviour is created in it (see Behaviour::SetEngineServices),
    // which is enough: there is only ever one EngineServices instance in the whole process, so
    // "duplicated but always converges to the same correct value" is harmless. The genuinely
    // per-callback state (Scene/EntityHandle, which really do change every call, so no
    // self-healing trick applies) lives as mutable fields ON that shared instance instead --
    // see EngineServices.h's own comment for why that specifically avoids the duplication bug.
    class ScriptContext {
    public:
        // Called from Behaviour::SetEngineServices (i.e. from GameScripts' own compiled code,
        // once per script instance created) so THIS binary's copy of s_Services is never left
        // null just because Bind() itself only ever runs on the engine/host side.
        static void EnsureBound(const EngineServices* services) {
            s_Services = services;
        }

        static void Bind(const EngineServices* services, void* scene, unsigned int entityHandle) {
            s_Services = services;
            if (services) {
                services->CurrentScene = scene;
                services->CurrentEntityHandle = entityHandle;
            }
        }

        static void Clear() {
            if (s_Services) {
                s_Services->CurrentScene = nullptr;
                s_Services->CurrentEntityHandle = 0;
            }
        }

        static const EngineServices* Services() { return s_Services; }
        static void* Scene() { return s_Services ? s_Services->CurrentScene : nullptr; }
        static unsigned int EntityHandle() { return s_Services ? s_Services->CurrentEntityHandle : 0; }

    private:
        static inline const EngineServices* s_Services = nullptr;
    };

}
