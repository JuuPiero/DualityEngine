#include "PatrolEnemyBehaviour.h"

#include <glm/glm.hpp>

#include "ScriptRegistration.h"

void PatrolEnemyBehaviour::OnCreate() {
    m_StartX = GetTransform().GetLocalPosition().x;
}

void PatrolEnemyBehaviour::OnUpdate(float deltaTime) {
    glm::vec3 position = GetTransform().GetLocalPosition();
    position.x += m_Direction * Speed * deltaTime;
    if (position.x > m_StartX + Range) {
        position.x = m_StartX + Range;
        m_Direction = -1.0f;
    } else if (position.x < m_StartX - Range) {
        position.x = m_StartX - Range;
        m_Direction = 1.0f;
    }
    GetTransform().SetLocalPosition(position);
}

REGISTER_BEHAVIOUR(PatrolEnemyBehaviour)
