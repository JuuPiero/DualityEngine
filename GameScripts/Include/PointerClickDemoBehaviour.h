#pragma once

#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/PointerEventHandlers.h"

// Demo for PhysicsRaycaster2D/3D -- implements IPointerClickHandler by overriding
// Behaviour::OnPointerClick. Tap PhysicsBall (Top/2D) or PhysicsBall3D (Bottom/3D) during Play.
class PointerClickDemoBehaviour : public Duality::Behaviour {
public:
    void OnPointerClick(Duality::PointerEventData& eventData) override;
};
