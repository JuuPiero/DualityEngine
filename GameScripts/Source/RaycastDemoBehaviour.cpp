#include "RaycastDemoBehaviour.h"

#include <string>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptDebug.h"
#include "DualityEngine/Scripting/Input.h"
#include "DualityEngine/Scripting/ScriptPhysics3D.h"
#include "ScriptRegistration.h"

void RaycastDemoBehaviour::OnUpdate(float deltaTime) {
    bool down = Duality::Input::GetPointerDown();
    bool justPressed = down && !m_WasDown;
    m_WasDown = down;

    // Edge-triggered (once per press, not every frame while held) and Bottom-screen-only --
    // GetPointerScreen() is what makes this safe even though Top and Bottom share overlapping
    // local pixel ranges (see Input::GetPointerScreen's own comment).
    if (!justPressed || Duality::Input::GetPointerScreen() != Duality::Screen::Bottom)
        return;

    glm::vec3 origin, direction;
    if (!Duality::ScriptPhysics3D::ScreenPointToRay(Duality::Screen::Bottom, Duality::Input::GetPointerPosition(), origin, direction)) {
        Duality::ScriptDebug::LogWarn("RaycastDemo: Bottom screen has no Perspective primary camera to raycast from");
        return;
    }

    Duality::RaycastHit3D hit = Duality::ScriptPhysics3D::Raycast(origin, direction, 20.0f);
    if (hit)
        Duality::ScriptDebug::LogInfo("RaycastDemo: hit '" + hit.HitEntity.GetComponent<Duality::NameComponent>().Name + "' at distance " + std::to_string(hit.Distance));
    else
        Duality::ScriptDebug::LogInfo("RaycastDemo: touched empty space");
}

REGISTER_BEHAVIOUR(RaycastDemoBehaviour)
