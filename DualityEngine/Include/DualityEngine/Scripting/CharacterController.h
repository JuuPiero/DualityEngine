#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity's CharacterController API for an entity's CharacterControllerComponent. Move/
    // SimpleMove run Bullet sweeps inside the engine, so they route through ScriptContext's
    // EngineServices bridge (header-only, works from GameScripts.dll). Remember +Y is DOWN:
    // gravity and "jump" motions use -Y for up.
    //
    //   CharacterController controller(GetEntity());
    //   controller.SimpleMove(glm::vec3(input.x, 0.0f, input.y) * speed);
    //   if (controller.IsGrounded() && Input::GetKeyDown(KeyCode::Space)) ...
    class CharacterController {
    public:
        explicit CharacterController(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<CharacterControllerComponent>(); }

        // Moves by `motion` (world units, NOT scaled by deltaTime), colliding and sliding.
        // Returns Unity's CollisionFlags (CollisionSides / CollisionAbove / CollisionBelow).
        int Move(const glm::vec3& motion) {
            const EngineServices* services = ScriptContext::Services();
            if (!services || !services->CharacterControllerMove || !m_Entity)
                return CollisionNone;
            return services->CharacterControllerMove(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), motion.x, motion.y, motion.z);
        }

        // Moves with `speed` (units / second, vertical part ignored) plus gravity. Call once per
        // frame. Returns IsGrounded.
        bool SimpleMove(const glm::vec3& speed) {
            const EngineServices* services = ScriptContext::Services();
            if (!services || !services->CharacterControllerSimpleMove || !m_Entity)
                return false;
            return services->CharacterControllerSimpleMove(m_Entity.GetScene(), static_cast<unsigned int>(m_Entity.Handle()), speed.x, speed.y, speed.z);
        }

        bool IsGrounded() const { return *this && m_Entity.GetComponent<CharacterControllerComponent>().IsGrounded; }
        int GetCollisionFlags() const { return *this ? m_Entity.GetComponent<CharacterControllerComponent>().CollisionFlags : CollisionNone; }
        glm::vec3 GetVelocity() const { return *this ? m_Entity.GetComponent<CharacterControllerComponent>().Velocity : glm::vec3(0.0f); }

    private:
        Entity m_Entity;
    };

}
