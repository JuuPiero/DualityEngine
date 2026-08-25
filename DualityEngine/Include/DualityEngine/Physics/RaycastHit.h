#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    // Unity's RaycastHit2D/RaycastHit -- the result of Scene::Raycast2D/Raycast3D
    // (Behaviour::Raycast2D/Raycast3D on the scripting side). HitEntity is falsy
    // (default-constructed Entity) when the ray hit nothing, matching every other query in
    // this engine's scripting API (FindEntityInScreen, Instantiate, ...) -- check it (or the
    // struct itself, via operator bool) before reading Point/Normal/Distance.
    struct RaycastHit2D {
        Entity HitEntity;
        glm::vec2 Point{ 0.0f };
        glm::vec2 Normal{ 0.0f };
        float Distance = 0.0f;

        operator bool() const { return static_cast<bool>(HitEntity); }
    };

    struct RaycastHit3D {
        Entity HitEntity;
        glm::vec3 Point{ 0.0f };
        glm::vec3 Normal{ 0.0f };
        float Distance = 0.0f;

        operator bool() const { return static_cast<bool>(HitEntity); }
    };

}
