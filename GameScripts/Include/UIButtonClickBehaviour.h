#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Logs a line to the Console whenever this entity's own Canvas UI Button is clicked.
class UIButtonClickBehaviour : public Duality::Behaviour {
public:
    void OnUpdate(float deltaTime) override;
};
