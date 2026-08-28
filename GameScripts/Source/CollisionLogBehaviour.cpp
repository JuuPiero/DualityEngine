#include "CollisionLogBehaviour.h"

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptDebug.h"
#include "ScriptRegistration.h"

using namespace Duality;

static std::string NameOf(Entity entity) {
    return entity.HasComponent<NameComponent>() ? entity.GetComponent<NameComponent>().Name : "<unnamed>";
}

void CollisionLogBehaviour::OnCollisionEnter(Entity other) {
    ScriptDebug::LogInfo("CollisionLogBehaviour: " + NameOf(GetEntity()) + " collided with " + NameOf(other));
}

void CollisionLogBehaviour::OnCollisionExit(Entity other) {
    ScriptDebug::LogInfo("CollisionLogBehaviour: " + NameOf(GetEntity()) + " stopped colliding with " + NameOf(other));
}

void CollisionLogBehaviour::OnTriggerEnter(Entity other) {
    ScriptDebug::LogInfo("CollisionLogBehaviour: " + NameOf(GetEntity()) + " entered trigger " + NameOf(other));
}

void CollisionLogBehaviour::OnTriggerExit(Entity other) {
    ScriptDebug::LogInfo("CollisionLogBehaviour: " + NameOf(GetEntity()) + " exited trigger " + NameOf(other));
}

REGISTER_BEHAVIOUR(CollisionLogBehaviour)
