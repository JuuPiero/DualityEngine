#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Bounces the entity up/down around its starting Y position. A trivial demo
// script, but a real one -- edited here, hot-reloaded into the running
// Editor via the "Reload Scripts" button, no code changes needed to also
// compile it statically into a future device build.
class BounceBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

private:
    float m_Time = 0.0f;
    float m_BaseY = 0.0f;
};
