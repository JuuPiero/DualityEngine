#include "BounceBehaviour.h"

#include <cmath>

#include "DualityEngine/Scene/Components.h"
#include "ScriptRegistration.h"

void BounceBehaviour::OnCreate() {
    m_BaseY = GetComponent<Duality::TransformComponent>().Translation.y;
}

void BounceBehaviour::OnUpdate(float deltaTime) {
    m_Time += deltaTime;
    GetComponent<Duality::TransformComponent>().Translation.y = m_BaseY + std::sin(m_Time * 8.0f) * 40.0f;
}

REGISTER_BEHAVIOUR(BounceBehaviour)
