#pragma once

#include <vector>

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // Reusable friction/restitution asset (.physmat) -- referenced by collider components.
    struct PhysicsMaterial {
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float Density = 1.0f;

        static std::vector<FieldHandle> Fields() {
            return {
                MakeField("Friction", &PhysicsMaterial::Friction),
                MakeField("Restitution", &PhysicsMaterial::Restitution),
                MakeField("Density", &PhysicsMaterial::Density),
            };
        }
    };

}
