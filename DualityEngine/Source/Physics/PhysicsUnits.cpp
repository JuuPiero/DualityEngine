#include "DualityEngine/Physics/PhysicsUnits.h"

#include <algorithm>

#include "DualityEngine/Project/Project.h"

namespace Duality {

    namespace {
        bool s_HasOverride = false;
        float s_OverridePPU = PhysicsUnits::DefaultPPU;
        float s_OverrideGravity = PhysicsUnits::DefaultGravity;

        float SanitizePPU(float value) {
            return std::max(value, 0.001f);
        }
    }

    float PhysicsUnits::PPU() {
        if (s_HasOverride)
            return SanitizePPU(s_OverridePPU);
        if (auto project = Project::GetActive())
            return SanitizePPU(project->GetConfig().PPU);
        return DefaultPPU;
    }

    float PhysicsUnits::Gravity() {
        if (s_HasOverride)
            return s_OverrideGravity;
        if (auto project = Project::GetActive())
            return project->GetConfig().Gravity;
        return DefaultGravity;
    }

    float PhysicsUnits::PhysicsGravity() {
        return Gravity() / PPU();
    }

    float PhysicsUnits::ToPhysics(float pixels) {
        return pixels / PPU();
    }

    float PhysicsUnits::ToWorld(float units) {
        return units * PPU();
    }

    void PhysicsUnits::SetRuntimeOverride(float ppu, float gravity) {
        s_HasOverride = true;
        s_OverridePPU = ppu;
        s_OverrideGravity = gravity;
    }

}
