#pragma once

#include "DualityEngine/Scene/Behaviour.h"

class ProjectDemoBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

    DUALITY_PROPERTY() float Speed = 1.0f;

    DUALITY_PROPERTIES_AUTO()
};
