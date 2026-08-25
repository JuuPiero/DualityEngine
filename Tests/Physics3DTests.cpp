// 3D physics (Bullet integration) regression coverage -- ported from this session's own
// ad hoc scratchpad verification (physics3d_test.cpp) into a permanent, CI-run suite.
#include "TestFramework.h"

#include <cmath>

#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/Components.h"

using namespace Duality;

namespace {
    bool IsFinite(const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
}

TEST_CASE("A dynamic sphere falls under gravity and lands on a static platform") {
    Scene scene;

    Entity ground = scene.CreateEntity("Ground");
    ground.GetComponent<TransformComponent>().Translation = { 0.0f, 150.0f, 0.0f };
    ground.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxCollider3DComponent>().Size = { 100.0f, 10.0f, 100.0f };

    Entity ball = scene.CreateEntity("Ball");
    ball.GetComponent<TransformComponent>().Translation = { 0.0f, -40.0f, 0.0f };
    ball.AddComponent<Rigidbody3DComponent>();
    auto& ballCollider = ball.AddComponent<SphereCollider3DComponent>();
    ballCollider.Radius = 20.0f;
    ballCollider.Restitution = 0.1f;

    scene.OnRuntimeStart();
    float firstY = ball.GetComponent<TransformComponent>().Translation.y;
    bool sawFalling = false, everNaN = false;
    float restY = firstY;

    for (int frame = 0; frame < 300; frame++) {
        scene.OnRuntimeUpdate(1.0f / 60.0f);
        const auto& t = ball.GetComponent<TransformComponent>();
        if (!IsFinite(t.Translation) || !IsFinite(t.Rotation))
            everNaN = true;
        if (frame == 30)
            sawFalling = t.Translation.y > firstY + 5.0f;
        if (frame == 299)
            restY = t.Translation.y;
    }
    scene.OnRuntimeStop();

    CHECK_SOFT(sawFalling, "fell under gravity within the first 30 frames");
    // Ground top surface is at Y=140; ball radius 20 -> rests with center around Y=120.
    CHECK_SOFT(restY > 100.0f && restY < 135.0f, "landed on the platform without tunneling through it");
    CHECK_SOFT(std::fabs(ground.GetComponent<TransformComponent>().Translation.y - 150.0f) < 0.01f, "static ground was never moved by the simulation");
    CHECK_SOFT(!everNaN, "no NaN/garbage ever produced");
}

TEST_CASE("A tilted dynamic box's rotation round-trips exactly through Bullet") {
    Scene scene;
    Entity box = scene.CreateEntity("TiltedBox");
    box.GetComponent<TransformComponent>().Translation = { 300.0f, -200.0f, 0.0f };
    box.GetComponent<TransformComponent>().Rotation = { 15.0f, 37.0f, -22.0f };
    box.AddComponent<Rigidbody3DComponent>();
    box.AddComponent<BoxCollider3DComponent>();

    scene.OnRuntimeStart();
    for (int frame = 0; frame < 300; frame++)
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    scene.OnRuntimeStop();

    const auto& rotation = box.GetComponent<TransformComponent>().Rotation;
    CHECK_SOFT(std::fabs(rotation.x - 15.0f) < 0.01f, "rotation.x survived 300 physics steps exactly");
    CHECK_SOFT(std::fabs(rotation.y - 37.0f) < 0.01f, "rotation.y survived 300 physics steps exactly");
    CHECK_SOFT(std::fabs(rotation.z - (-22.0f)) < 0.01f, "rotation.z survived 300 physics steps exactly");
}

TEST_CASE("A non-zero collider Offset (compound shape) does not move the body itself") {
    Scene scene;
    Entity box = scene.CreateEntity("OffsetBox");
    box.GetComponent<TransformComponent>().Translation = { -300.0f, 0.0f, 0.0f };
    box.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    auto& collider = box.AddComponent<BoxCollider3DComponent>();
    collider.Offset = { 25.0f, -10.0f, 5.0f };
    collider.Size = { 15.0f, 15.0f, 15.0f };

    scene.OnRuntimeStart();
    for (int frame = 0; frame < 60; frame++)
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    scene.OnRuntimeStop(); // must not crash cleaning up the compound-shape wrapper

    const auto& t = box.GetComponent<TransformComponent>();
    CHECK_SOFT(IsFinite(t.Translation), "offset-collider body's transform stayed finite");
    CHECK_SOFT(std::fabs(t.Translation.x + 300.0f) < 0.01f && std::fabs(t.Translation.y) < 0.01f && std::fabs(t.Translation.z) < 0.01f,
        "Offset only shifts the collision volume, never the body/entity transform itself");
}
