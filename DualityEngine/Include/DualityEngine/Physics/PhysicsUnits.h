#pragma once

namespace Duality {

    // 1 unit is not a meter — pick any scale that keeps solver values comfortable.
    // Current convention (version 2): public world values are Unity-style units.
    // PPU is consumed solely by SceneRenderer for 2D world-to-screen conversion.
    class PhysicsUnits {
    public:
        static constexpr float DefaultPPU = 100.0f;
        static constexpr float DefaultGravity = 9.81f; // world units / s^2, +Y down

        static float PPU();
        static float Gravity(); // world units / s^2
        static float PhysicsGravity(); // same value, fed directly to Box2D/Bullet

        static float ToPhysics(float worldUnits);
        static float ToWorld(float physicsUnits);

        // 3DS player has no .dproj — BuildPipeline bakes these into BuildSettings.json.
        static void SetRuntimeOverride(float ppu, float gravity);
    };

}
