#pragma once
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Components.h"

using namespace Duality;

class BallTest: public Behaviour
{

public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

    
    float velocityY = 0;
    float friction = 0.99;
    Entity buttonEntity;

    // DUALITY_PROPERTIES(BounceBehaviour, buttonEntity)
    // SpriteRendererComponent sprite;
};

