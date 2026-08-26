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

    // Wobble.Amount defaults to 0 -- a no-op, this demo's original single-axis bounce is
    // unchanged unless the user actually opens the nested Wobble group in Properties and raises
    // it above zero.
    if (Wobble.Amount != 0.0f) {
        auto& transform = GetComponent<Duality::TransformComponent>();
        transform.Translation.x += std::sin(m_Time * Wobble.Frequency) * Wobble.Amount * deltaTime;
    }
}

REGISTER_BEHAVIOUR(BounceBehaviour)
