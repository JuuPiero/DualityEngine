#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Physics/RaycastHit.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity's Physics class -- static raycast queries against the live Bullet world in
    // ScriptContext's bound Scene (only valid while Play is running).
    class ScriptPhysics3D {
    public:
        static RaycastHit3D Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
            RaycastHit3D hit;
            const EngineServices* services = ScriptContext::Services();
            void* scene = ScriptContext::Scene();
            if (!services || !scene)
                return hit;

            unsigned int handle = 0;
            float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, dist = 0;
            if (!services->Raycast3D(scene, origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance,
                                     &handle, &px, &py, &pz, &nx, &ny, &nz, &dist))
                return hit;

            hit.HitEntity = Entity(static_cast<entt::entity>(handle), static_cast<Scene*>(scene));
            hit.Point = { px, py, pz };
            hit.Normal = { nx, ny, nz };
            hit.Distance = dist;
            return hit;
        }

        // Converts a point in `screen`'s own local pixel space (e.g. Input::GetPointerPosition(),
        // with `screen` = Input::GetPointerScreen()) into a world-space ray from that screen's
        // primary camera -- feed the result straight into Raycast for click/touch-to-select gameplay.
        // Returns false (outOrigin/outDirection untouched) if that screen has no Perspective primary
        // camera, or Play isn't running yet.
        static bool ScreenPointToRay(Screen screen, const glm::vec2& screenPoint, glm::vec3& outOrigin, glm::vec3& outDirection) {
            const EngineServices* services = ScriptContext::Services();
            void* scene = ScriptContext::Scene();
            if (!services || !scene)
                return false;

            float ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0;
            if (!services->ScreenPointToRay3D(scene, static_cast<int>(screen), screenPoint.x, screenPoint.y,
                                              &ox, &oy, &oz, &dx, &dy, &dz))
                return false;

            outOrigin = { ox, oy, oz };
            outDirection = { dx, dy, dz };
            return true;
        }
    };

}
