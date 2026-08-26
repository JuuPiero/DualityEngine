#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Bounces the entity up/down around its starting Y position (or, if Target is
// set, around that other entity's current Y position instead). A trivial demo
// script, but a real one -- edited here, hot-reloaded into the running
// Editor via the "Reload Scripts" button, no code changes needed to also
// compile it statically into a future device build. Also the demo script for
// DUALITY_PROPERTY (Inspector-editable public fields) and EntityRef
// (drag-drop an entity from the Hierarchy onto the Target field).
class BounceBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

    DUALITY_PROPERTY() float Amplitude = 40.0f;
    DUALITY_PROPERTY() float Speed = 8.0f;
    DUALITY_PROPERTY() Duality::EntityRef Target;

    DUALITY_PROPERTIES_AUTO()

private:
    float m_Time = 0.0f;
    float m_BaseY = 0.0f;
};
