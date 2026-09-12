#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Demonstrates every scripting-facing API built so far in one place --
// Input (keys/axes/pointer), Transform, SaveSystem, DateTime, and
// ScriptAudio -- living, runnable documentation rather than
// a real gameplay script. See README.md's "Writing gameplay scripts"
// section for what each call does and why.
//
// Entity-shape-agnostic: the exact same script class works whether it's attached to a 2D
// sprite entity or a 3D mesh entity (GetEntity().HasComponent<T>() branches at runtime, see
// OnCreate/OnUpdate) -- a Behaviour never assumes its entity's exact component makeup, matching
// Unity's own "components are optional" convention.
class ApiShowcaseBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

private:
    int m_PlayCount = 0;
};
