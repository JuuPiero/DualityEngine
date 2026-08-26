#pragma once

#include "DualityEngine/Scene/Behaviour.h"

class NewBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;
};
