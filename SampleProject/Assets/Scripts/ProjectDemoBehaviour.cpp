#include "ProjectDemoBehaviour.h"

#include "DualityEngine/Scripting/ScriptDebug.h"
#include "ScriptRegistration.h"

void ProjectDemoBehaviour::OnCreate() {
    Duality::ScriptDebug::LogInfo("ProjectDemoBehaviour: OnCreate (proves a project-owned script compiled and ran)");
}

void ProjectDemoBehaviour::OnUpdate(float deltaTime) {
}

REGISTER_BEHAVIOUR(ProjectDemoBehaviour)
