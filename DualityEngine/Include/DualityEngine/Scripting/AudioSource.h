#pragma once

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scripting/EngineServices.h"

namespace Duality {

    // Unity's AudioSource component API -- Play/Stop/Pause/Volume on THIS entity's own
    // AudioSourceComponent (routes through EngineServices). Obtain from
    // Behaviour::GetAudioSource(), not GetComponent (the ECS component holds authored
    // fields + runtime handle bookkeeping).
    class AudioSource {
    public:
        AudioSource(Entity entity, const EngineServices* services) : m_Entity(entity), m_Services(services) {}

        void Play() {
            if (m_Services && m_Entity)
                m_Services->AudioSourcePlay(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()));
        }

        void Stop() {
            if (m_Services && m_Entity)
                m_Services->AudioSourceStop(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()));
        }

        void Pause() {
            if (m_Services && m_Entity)
                m_Services->AudioSourceSetPaused(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), true);
        }

        void UnPause() {
            if (m_Services && m_Entity)
                m_Services->AudioSourceSetPaused(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), false);
        }

        void SetVolume(float volume) {
            if (m_Services && m_Entity)
                m_Services->AudioSourceSetVolume(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), volume);
        }

        bool IsPlaying() const {
            if (!m_Services || !m_Entity)
                return false;
            return m_Services->AudioSourceIsPlaying(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()));
        }

    private:
        Entity m_Entity;
        const EngineServices* m_Services = nullptr;
    };

}
