#pragma once

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // SpriteFlipbookComponent playback controls.
    class SpriteFlipbook {
    public:
        explicit SpriteFlipbook(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<SpriteFlipbookComponent>(); }

        bool IsPlaying() const {
            return m_Entity ? m_Entity.GetComponent<SpriteFlipbookComponent>().Playing : false;
        }
        void Play() {
            if (*this)
                m_Entity.GetComponent<SpriteFlipbookComponent>().Playing = true;
        }
        void Stop() {
            if (*this)
                m_Entity.GetComponent<SpriteFlipbookComponent>().Playing = false;
        }

        bool GetLoop() const {
            return m_Entity ? m_Entity.GetComponent<SpriteFlipbookComponent>().Loop : true;
        }
        void SetLoop(bool loop) {
            if (*this)
                m_Entity.GetComponent<SpriteFlipbookComponent>().Loop = loop;
        }

        float GetFrameDuration() const {
            return m_Entity ? m_Entity.GetComponent<SpriteFlipbookComponent>().FrameDuration : 0.1f;
        }
        void SetFrameDuration(float seconds) {
            if (*this)
                m_Entity.GetComponent<SpriteFlipbookComponent>().FrameDuration = seconds;
        }

        int GetCurrentFrame() const {
            return m_Entity ? m_Entity.GetComponent<SpriteFlipbookComponent>().CurrentFrame : 0;
        }

    private:
        Entity m_Entity;
    };

}
