#pragma once

// Transform-driving helpers evaluated in Play mode: Follow Target and Unity's constraint family
// (Position / Rotation / Scale / Parent / Look At / Aim). Part of Scene/Components.h.

#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    struct FollowTargetComponent {
        bool Enabled = true;
        EntityRef Target;
        glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
        float SmoothSpeed = 0.0f; // 0 = snap each frame
        bool FollowX = true;
        bool FollowY = true;
        bool FollowZ = false;
    };

    enum class ConstraintWorldUpType { SceneUp, ObjectUp, ObjectRotationUp, Vector, None };

    // Unity constraints (Play mode, evaluated after LateUpdate). With several Sources the result
    // is their average. Weight blends from the At Rest value to the constrained one.
    struct PositionConstraintComponent {
        bool Enabled = true;
        float Weight = 1.0f;
        std::vector<EntityRef> Sources;
        glm::vec3 TranslationAtRest{ 0.0f };
        glm::vec3 TranslationOffset{ 0.0f };
        bool ConstrainX = true, ConstrainY = true, ConstrainZ = true;
    };

    struct RotationConstraintComponent {
        bool Enabled = true;
        float Weight = 1.0f;
        std::vector<EntityRef> Sources;
        glm::vec3 RotationAtRest{ 0.0f };
        glm::vec3 RotationOffset{ 0.0f };
        bool ConstrainX = true, ConstrainY = true, ConstrainZ = true;
    };

    struct ScaleConstraintComponent {
        bool Enabled = true;
        float Weight = 1.0f;
        std::vector<EntityRef> Sources;
        glm::vec3 ScaleAtRest{ 1.0f };
        glm::vec3 ScaleOffset{ 1.0f };
        bool ConstrainX = true, ConstrainY = true, ConstrainZ = true;
    };

    // Follows Source as if parented to it. With Auto Offset the current pose relative to the
    // source is captured when Play starts (Unity's "Activate"); otherwise the authored offsets
    // are used.
    struct ParentConstraintComponent {
        bool Enabled = true;
        float Weight = 1.0f;
        EntityRef Source;
        bool AutoOffset = true;
        glm::vec3 TranslationOffset{ 0.0f };
        glm::vec3 RotationOffset{ 0.0f };
        bool ConstrainTranslation = true;
        bool ConstrainRotation = true;
        // Runtime.
        bool RuntimeCaptured = false;
    };

    // Points the entity's forward (-Z, the engine's camera/light forward) at Source.
    struct LookAtConstraintComponent {
        bool Enabled = true;
        float Weight = 1.0f;
        EntityRef Source;
        float Roll = 0.0f;
        bool UseUpObject = false;
        EntityRef WorldUpObject;
        glm::vec3 RotationOffset{ 0.0f };
    };

    // Points Aim Vector (local) at the average of Sources, keeping Up Vector towards World Up.
    // For 2D use Aim Vector (1,0,0) and World Up Type None (rotation around Z only).
    struct AimConstraintComponent {
        bool Enabled = true;
        float Weight = 1.0f;
        std::vector<EntityRef> Sources;
        glm::vec3 AimVector{ 0.0f, 0.0f, -1.0f };
        glm::vec3 UpVector{ 0.0f, -1.0f, 0.0f };
        ConstraintWorldUpType WorldUpType = ConstraintWorldUpType::SceneUp;
        glm::vec3 WorldUpVector{ 0.0f, -1.0f, 0.0f };
        EntityRef WorldUpObject;
        glm::vec3 RotationOffset{ 0.0f };
    };

}
