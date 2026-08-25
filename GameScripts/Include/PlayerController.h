#pragma once

#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Components.h"


class PlayerController : public Duality::Behaviour 
{
public:
    void OnCreate() override;


public:
    float speed = 5;
   
};

