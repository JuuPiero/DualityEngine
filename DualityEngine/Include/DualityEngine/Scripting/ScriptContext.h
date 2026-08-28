#pragma once

#include "DualityEngine/Scripting/EngineServices.h"

namespace Duality {

    // Per-callback execution context for Unity-style static scripting APIs
    // (ScriptInput, ScriptPhysics2D/3D, ScriptDebug, ScriptAudio, ScriptScene).
    // Scene binds this right before every Behaviour lifecycle/collision callback
    // and clears it afterward -- scripts never call Bind/Clear themselves.
    class ScriptContext {
    public:
        static void Bind(const EngineServices* services, void* scene, unsigned int entityHandle) {
            s_Services = services;
            s_Scene = scene;
            s_EntityHandle = entityHandle;
        }

        static void Clear() {
            s_Services = nullptr;
            s_Scene = nullptr;
            s_EntityHandle = 0;
        }

        static const EngineServices* Services() { return s_Services; }
        static void* Scene() { return s_Scene; }
        static unsigned int EntityHandle() { return s_EntityHandle; }

    private:
        static inline const EngineServices* s_Services = nullptr;
        static inline void* s_Scene = nullptr;
        static inline unsigned int s_EntityHandle = 0;
    };

}
