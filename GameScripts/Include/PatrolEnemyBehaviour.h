#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Moves back and forth along X between [start-Range, start+Range] at a steady Speed -- pair
// with a Kinematic Rigidbody2D (moved via this script's own Transform writes, which Scene.cpp
// pushes into the physics body every frame for a Kinematic body -- see its own comment) + a
// trigger collider + TagComponent{"Enemy"} for a classic patrolling platformer hazard (see
// PlayerController::OnTriggerEnter's "Enemy" handling).
class PatrolEnemyBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

    DUALITY_PROPERTY() float Speed = 0.4f;
    DUALITY_PROPERTY() float Range = 0.6f;

    DUALITY_PROPERTIES_AUTO()

private:
    float m_StartX = 0.0f;
    float m_Direction = 1.0f;
};
