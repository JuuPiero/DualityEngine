#include "BounceBehaviour.h"

#include <cmath>

#include "DualityEngine/Scene/Components.h"
#include "ScriptRegistration.h"

void BounceBehaviour::OnCreate() {
    m_BaseY = GetComponent<Duality::TransformComponent>().Translation.y;
}

void BounceBehaviour::OnUpdate(float deltaTime) {
    m_Time += deltaTime;
    float baseY = m_BaseY;
    Duality::Entity target = ResolveEntityRef(Target);
    if (target)
        baseY = target.GetComponent<Duality::TransformComponent>().Translation.y;
    GetComponent<Duality::TransformComponent>().Translation.y = baseY + std::sin(m_Time * Speed) * Amplitude;
}

REGISTER_BEHAVIOUR(BounceBehaviour)
