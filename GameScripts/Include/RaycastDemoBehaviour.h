#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Click/touch-to-select demo for the physics Raycast API -- not attached to any particular
// object (it doesn't need a Transform/collider of its own), just watches the Bottom screen's
// pointer and logs whichever 3D collider it hits. Doubles as living documentation for
// Behaviour::ScreenPointToRay3D + Raycast3D.
class RaycastDemoBehaviour : public Duality::Behaviour {
public:
    void OnUpdate(float deltaTime) override;

private:
    bool m_WasDown = false;
};
