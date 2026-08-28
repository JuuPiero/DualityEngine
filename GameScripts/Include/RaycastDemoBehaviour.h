#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Click/touch-to-select demo for ScriptPhysics3D::ScreenPointToRay + Raycast.
class RaycastDemoBehaviour : public Duality::Behaviour {
public:
    void OnUpdate(float deltaTime) override;

private:
    bool m_WasDown = false;
};
