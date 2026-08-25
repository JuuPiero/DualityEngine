#include "RaycastDemoBehaviour.h"

#include <string>

#include "DualityEngine/Scene/Components.h"
#include "ScriptRegistration.h"

void RaycastDemoBehaviour::OnUpdate(float deltaTime) {
    bool down = GetPointerDown();
    bool justPressed = down && !m_WasDown;
    m_WasDown = down;

    // Edge-triggered (once per press, not every frame while held) and Bottom-screen-only --
    // GetPointerScreen() is what makes this safe even though Top and Bottom share overlapping
    // local pixel ranges (see Input::GetPointerScreen's own comment).
    if (!justPressed || GetPointerScreen() != Duality::Screen::Bottom)
        return;

    glm::vec3 origin, direction;
    if (!ScreenPointToRay3D(Duality::Screen::Bottom, GetPointerPosition(), origin, direction)) {
        LogWarn("RaycastDemo: Bottom screen has no Perspective primary camera to raycast from");
        return;
    }

    Duality::RaycastHit3D hit = Raycast3D(origin, direction, 2000.0f);
    if (hit)
        LogInfo("RaycastDemo: hit '" + hit.HitEntity.GetComponent<Duality::NameComponent>().Name + "' at distance " + std::to_string(hit.Distance));
    else
        LogInfo("RaycastDemo: touched empty space");
}

REGISTER_BEHAVIOUR(RaycastDemoBehaviour)
