#include "PointerClickDemoBehaviour.h"

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptDebug.h"
#include "ScriptRegistration.h"

void PointerClickDemoBehaviour::OnPointerClick(Duality::PointerEventData& eventData) {
    Duality::ScriptDebug::LogInfo("PointerClickDemo: clicked '" + GetName() + "' at world ("
        + std::to_string(eventData.WorldPoint.x) + ", "
        + std::to_string(eventData.WorldPoint.y) + ", "
        + std::to_string(eventData.WorldPoint.z) + ")");
}

REGISTER_BEHAVIOUR(PointerClickDemoBehaviour)
