#pragma once

#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/PointerEventHandlers.h"

// Demo for PhysicsRaycaster2D/3D. Pointer callbacks are opt-in, rather than built into
// Behaviour, so this script explicitly implements the EventSystem interface.
class PointerClickDemoBehaviour : public Duality::Behaviour, public Duality::IPointerClickHandler {
public:
    void OnPointerClick(Duality::PointerEventData& eventData) override;
};
