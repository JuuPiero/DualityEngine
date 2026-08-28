#include "BounceBehaviour.h"

#include <cmath>

#include "DualityEngine/Scene/Components.h"
#include "ScriptRegistration.h"

void BounceBehaviour::OnCreate() {
    m_BaseY = GetTransform().GetLocalPosition().y;
}

void BounceBehaviour::OnUpdate(float deltaTime) {
    m_Time += deltaTime;
    float baseY = m_BaseY;
    Duality::Entity target = ResolveEntityRef(Target);
    if (target)
        baseY = Duality::Transform(target).GetLocalPosition().y;
    GetTransform().SetLocalPosition({ GetTransform().GetLocalPosition().x, baseY + std::sin(m_Time * Speed) * Amplitude, GetTransform().GetLocalPosition().z });

    // Wobble.Amount defaults to 0 -- a no-op, this demo's original single-axis bounce is
    // unchanged unless the user actually opens the nested Wobble group in Properties and raises
    // it above zero.
    if (Wobble.Amount != 0.0f) {
        glm::vec3 pos = GetTransform().GetLocalPosition();
        pos.x += std::sin(m_Time * Wobble.Frequency) * Wobble.Amount * deltaTime;
        GetTransform().SetLocalPosition(pos);
    }
}

REGISTER_BEHAVIOUR(BounceBehaviour)
