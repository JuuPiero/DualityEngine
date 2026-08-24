#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Logs every collision/trigger enter/exit involving this entity to the Editor's Console
// panel via the new Debug.Log bridge -- attach to any entity with a Rigidbody2D/3D +
// collider to see OnCollisionEnter/Exit/OnTriggerEnter/Exit fire in practice. A living
// demo/smoke-test for that new lifecycle surface, same "small, focused, copy-me" role
// BounceBehaviour plays for the physics-body basics.
class CollisionLogBehaviour : public Duality::Behaviour {
public:
    void OnCollisionEnter(Duality::Entity other) override;
    void OnCollisionExit(Duality::Entity other) override;
    void OnTriggerEnter(Duality::Entity other) override;
    void OnTriggerExit(Duality::Entity other) override;
};
