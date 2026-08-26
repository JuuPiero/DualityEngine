#include "ProjectDemoBehaviour.h"

#include "ScriptRegistration.h"

void ProjectDemoBehaviour::OnCreate() {
    LogInfo("ProjectDemoBehaviour: OnCreate (proves a project-owned script compiled and ran)");
}

void ProjectDemoBehaviour::OnUpdate(float deltaTime) {
}

REGISTER_BEHAVIOUR(ProjectDemoBehaviour)
