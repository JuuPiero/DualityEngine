// BodyType (Static/Kinematic/Dynamic) regression coverage -- Kinematic is new this pass,
// replacing the old plain `bool IsStatic`. Kinematic must satisfy two things a boolean
// couldn't express: (1) never affected by gravity/forces, same as Static, but (2) its
// TransformComponent (as set directly by a script, simulating script-driven movement) is
// pushed INTO the physics body every frame before stepping, unlike Static which never moves at
// all -- see Scene::OnRuntimeUpdate's own push-before-step loops for both Box2D and Bullet.
#include "TestFramework.h"

#include <cmath>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

using namespace Duality;

TEST_CASE("A 2D Kinematic body ignores gravity but tracks script-driven Transform changes") {
    Scene scene;
    Entity platform = scene.CreateEntity("KinematicPlatform");
    platform.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    platform.AddComponent<Rigidbody2DComponent>().Type = BodyType::Kinematic;
    platform.AddComponent<BoxCollider2DComponent>();

    scene.OnRuntimeStart();
    for (int frame = 0; frame < 30; frame++) {
        // Simulating a script moving this platform frame by frame, e.g. via a
        // Behaviour::OnUpdate directly mutating GetComponent<TransformComponent>().
        platform.GetComponent<TransformComponent>().Translation.x = static_cast<float>(frame) * 5.0f;
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    scene.OnRuntimeStop();

    const auto& t = platform.GetComponent<TransformComponent>();
    CHECK_SOFT(std::fabs(t.Translation.y) < 0.01f, "Kinematic body never falls under gravity, unlike Dynamic");
    CHECK_SOFT(std::fabs(t.Translation.x - 145.0f) < 0.01f, "Kinematic body's X tracks the script-driven value set on the last frame (frame 29 * 5.0)");
}

TEST_CASE("A 3D Kinematic body ignores gravity but tracks script-driven Transform changes") {
    Scene scene;
    Entity platform = scene.CreateEntity("KinematicPlatform3D");
    platform.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    platform.AddComponent<Rigidbody3DComponent>().Type = BodyType::Kinematic;
    platform.AddComponent<BoxCollider3DComponent>();

    scene.OnRuntimeStart();
    for (int frame = 0; frame < 30; frame++) {
        platform.GetComponent<TransformComponent>().Translation.z = static_cast<float>(frame) * 4.0f;
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    scene.OnRuntimeStop();

    const auto& t = platform.GetComponent<TransformComponent>();
    CHECK_SOFT(std::fabs(t.Translation.y) < 0.01f, "Kinematic body never falls under gravity, unlike Dynamic");
    CHECK_SOFT(std::fabs(t.Translation.z - 116.0f) < 0.01f, "Kinematic body's Z tracks the script-driven value set on the last frame (frame 29 * 4.0)");
}

TEST_CASE("A moving 2D Kinematic body still pushes a Dynamic body out of its way") {
    // The real point of Kinematic over the old plain Static: it still generates full collision
    // response against Dynamic bodies, it's just never itself moved by them/gravity. The
    // platform is tall (large Y half-extent) and reaches the ball within a handful of frames --
    // the ball is a free-falling Dynamic body with no floor of its own, so the test needs to
    // resolve the push well before gravity carries it out of the platform's vertical range
    // entirely (a real first-draft bug here: 400 units/s^2 gravity over ~0.5s of "platform still
    // approaching" is already tens of units of fall).
    Scene scene;
    Entity platform = scene.CreateEntity("KinematicPusher");
    platform.GetComponent<TransformComponent>().Translation = { -60.0f, 0.0f, 0.0f };
    platform.AddComponent<Rigidbody2DComponent>().Type = BodyType::Kinematic;
    platform.AddComponent<BoxCollider2DComponent>().Size = { 20.0f, 60.0f };

    Entity ball = scene.CreateEntity("DynamicBall");
    ball.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    ball.AddComponent<Rigidbody2DComponent>();
    ball.AddComponent<CircleCollider2DComponent>().Radius = 10.0f;

    scene.OnRuntimeStart();
    for (int frame = 0; frame < 50; frame++) {
        // Drives the platform steadily rightward, through and past the ball's starting spot.
        platform.GetComponent<TransformComponent>().Translation.x = -60.0f + static_cast<float>(frame) * 3.0f;
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    scene.OnRuntimeStop();

    CHECK_SOFT(ball.GetComponent<TransformComponent>().Translation.x > 5.0f, "the ball was pushed away from its start, not left sitting where the platform drove through it");
}
