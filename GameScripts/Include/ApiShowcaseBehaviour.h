#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Demonstrates every scripting-facing API built so far in one place --
// Input (keys/axes/pointer), Transform, SaveSystem, DateTime, and
// AudioEngine (via PlaySound) -- living, runnable documentation rather than
// a real gameplay script. See README.md's "Writing gameplay scripts"
// section for what each call does and why.
class ApiShowcaseBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

private:
    int m_PlayCount = 0;
};
