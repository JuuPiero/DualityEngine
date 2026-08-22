#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Behaviour.h"

namespace Duality {

    struct TagComponent {
        std::string Tag;
    };

    // Position/rotation/scale is kept as full 3D vectors even though only 2D
    // rendering exists so far -- the architecture is 2D-first, not 2D-only.
    struct TransformComponent {
        glm::vec3 Translation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f, 1.0f, 1.0f };
    };

    // No texture support yet (Phase 0) -- a flat-colored quad of Size pixels
    // centered on the entity's TransformComponent::Translation.
    struct SpriteRendererComponent {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec2 Size{ 32.0f, 32.0f };
    };

    // Screen is which physical 3DS screen this camera renders to -- the
    // concrete mechanism behind "dual-screen aware from the start".
    struct CameraComponent {
        Duality::Screen Screen = Duality::Screen::Top;
        bool Primary = true;
    };

    // Which Behaviour subclass is attached, looked up by name at Play time
    // via ScriptRegistry -- not a compile-time binding, so the Inspector can
    // show/edit it as plain text (like Unity picking a MonoBehaviour by
    // class) and so a hot-reloaded script DLL can supply a new instance
    // without the entity needing to change.
    struct BehaviourComponent {
        std::string ClassName;
        Behaviour* Instance = nullptr;
        void (*Destroy)(Behaviour*) = nullptr;
    };

}
