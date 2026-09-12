#pragma once

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity's AudioSource component API -- Play/Stop/Pause/Volume on an entity's own
    // AudioSourceComponent. It is intentionally separate from Behaviour: create it explicitly
    // with `AudioSource(entity)` after including this header. Runtime calls cross the script
    // binary boundary through ScriptContext, exactly like Input and ScriptAudio.
    class AudioSource {
    public:
        explicit AudioSource(Entity entity) : m_Entity(entity) {}

        void Play() {
            if (const EngineServices* services = ScriptContext::Services(); services && m_Entity)
                services->AudioSourcePlay(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()));
        }

        void Stop() {
            if (const EngineServices* services = ScriptContext::Services(); services && m_Entity)
                services->AudioSourceStop(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()));
        }

        void Pause() {
            if (const EngineServices* services = ScriptContext::Services(); services && m_Entity)
                services->AudioSourceSetPaused(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), true);
        }

        void UnPause() {
            if (const EngineServices* services = ScriptContext::Services(); services && m_Entity)
                services->AudioSourceSetPaused(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), false);
        }

        void SetVolume(float volume) {
            if (const EngineServices* services = ScriptContext::Services(); services && m_Entity)
                services->AudioSourceSetVolume(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), volume);
        }

        bool IsPlaying() const {
            const EngineServices* services = ScriptContext::Services();
            if (!services || !m_Entity)
                return false;
            return services->AudioSourceIsPlaying(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()));
        }

    private:
        Entity m_Entity;
    };

}
