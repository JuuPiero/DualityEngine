#pragma once

namespace Duality {

    // Scene Transform stays in pixels (1 unit = 1 pixel, matching the renderer).
    // Box2D/Bullet simulate in generic units: physics = pixels / PPU.
    // 1 unit is not a meter — pick any scale that keeps solver values comfortable.
    // Default 1 keeps existing scenes and scripts identical. Raise it (32 or 100)
    // in Project Settings so the solver sees Unity-sized objects; Rigidbody
    // velocity/force and raycasts stay in scene pixels either way.
    class PhysicsUnits {
    public:
        static constexpr float DefaultPPU = 1.0f;
        static constexpr float DefaultGravity = 400.0f; // scene pixels / s^2, +Y down

        static float PPU();
        static float Gravity(); // scene pixels / s^2
        static float PhysicsGravity(); // units / s^2, fed to Box2D/Bullet

        static float ToPhysics(float pixels);
        static float ToWorld(float units);

        // 3DS player has no .dproj — BuildPipeline bakes these into BuildSettings.json.
        static void SetRuntimeOverride(float ppu, float gravity);
    };

}
