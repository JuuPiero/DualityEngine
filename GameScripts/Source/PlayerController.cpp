#include "PlayerController.h"
#include "DualityEngine/Scripting/ScriptDebug.h"
#include "ScriptRegistration.h"
#include <string>

void PlayerController::OnCreate()
{
    auto entity = GetEntity();

    Duality::ScriptDebug::LogInfo("speed "  + std::to_string(speed));
    // Rigidbody3DComponent& rb = entity.AddComponent<Rigidbody3DComponent>();
}

REGISTER_BEHAVIOUR(PlayerController)
