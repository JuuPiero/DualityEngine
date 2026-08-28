#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Physics/RaycastHit.h"
#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity's Physics2D class -- static raycast queries against the live Box2D world
    // in ScriptContext's bound Scene (only valid while Play is running).
    class ScriptPhysics2D {
    public:
        static RaycastHit2D Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) {
            RaycastHit2D hit;
            const EngineServices* services = ScriptContext::Services();
            void* scene = ScriptContext::Scene();
            if (!services || !scene)
                return hit;

            unsigned int handle = 0;
            float px = 0, py = 0, nx = 0, ny = 0, dist = 0;
            if (!services->Raycast2D(scene, origin.x, origin.y, direction.x, direction.y, maxDistance,
                                     &handle, &px, &py, &nx, &ny, &dist))
                return hit;

            hit.HitEntity = Entity(static_cast<entt::entity>(handle), static_cast<Scene*>(scene));
            hit.Point = { px, py };
            hit.Normal = { nx, ny };
            hit.Distance = dist;
            return hit;
        }
    };

}
