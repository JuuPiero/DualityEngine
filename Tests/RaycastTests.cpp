// Physics Raycast API (Scene::Raycast2D/Raycast3D/ScreenPointToRay3D) regression coverage.
#include "TestFramework.h"

#include <cmath>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

using namespace Duality;

TEST_CASE("Raycast3D hits a static sphere collider directly ahead") {
    Scene scene;
    Entity sphere = scene.CreateEntity("Sphere");
    sphere.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, -100.0f };
    sphere.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    sphere.AddComponent<SphereCollider3DComponent>().Radius = 20.0f;

    scene.OnRuntimeStart();
    RaycastHit3D hit = scene.Raycast3D({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 500.0f);
    scene.OnRuntimeStop();

    CHECK(hit);
    CHECK_SOFT(hit.HitEntity == sphere, "hit the sphere entity, not some other/no entity");
    CHECK_SOFT(std::abs(hit.Distance - 80.0f) < 1.0f, "distance is to the NEAR surface (100 - radius 20), not the center");
    CHECK_SOFT(std::abs(hit.Point.z - (-80.0f)) < 1.0f, "hit point lands on the sphere's near surface");
}

TEST_CASE("Raycast3D misses when nothing is in the ray's path") {
    Scene scene;
    Entity sphere = scene.CreateEntity("Sphere");
    sphere.GetComponent<TransformComponent>().Translation = { 500.0f, 500.0f, 500.0f }; // well off to the side
    sphere.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    sphere.AddComponent<SphereCollider3DComponent>().Radius = 20.0f;

    scene.OnRuntimeStart();
    RaycastHit3D hit = scene.Raycast3D({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 500.0f);
    scene.OnRuntimeStop();

    CHECK_SOFT(!hit, "no collider anywhere near the ray -- should report a miss");
}

TEST_CASE("Raycast3D returns a falsy hit when Play isn't running") {
    Scene scene;
    Entity sphere = scene.CreateEntity("Sphere");
    sphere.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, -100.0f };
    sphere.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    sphere.AddComponent<SphereCollider3DComponent>().Radius = 20.0f;

    // Deliberately no OnRuntimeStart() -- no btDiscreteDynamicsWorld exists yet to query.
    RaycastHit3D hit = scene.Raycast3D({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 500.0f);
    CHECK_SOFT(!hit, "raycasting before Play starts should fail gracefully, not crash");
}

TEST_CASE("Raycast2D hits a static circle collider directly ahead") {
    Scene scene;
    Entity circle = scene.CreateEntity("Circle");
    circle.GetComponent<TransformComponent>().Translation = { 0.0f, 100.0f, 0.0f };
    circle.AddComponent<Rigidbody2DComponent>().Type = BodyType::Static;
    circle.AddComponent<CircleCollider2DComponent>().Radius = 20.0f;

    scene.OnRuntimeStart();
    RaycastHit2D hit = scene.Raycast2D({ 0.0f, 0.0f }, { 0.0f, 1.0f }, 500.0f);
    scene.OnRuntimeStop();

    CHECK(hit);
    CHECK_SOFT(hit.HitEntity == circle, "hit the circle entity");
    CHECK_SOFT(std::abs(hit.Distance - 80.0f) < 1.0f, "distance is to the near surface (100 - radius 20)");
}

TEST_CASE("ScreenPointToRay3D returns the camera's forward direction for the screen center") {
    Scene scene;
    Entity camera = scene.CreateEntity("MainCamera");
    camera.GetComponent<TransformComponent>().Translation = { 5.0f, 10.0f, 50.0f };
    auto& cameraComponent = camera.AddComponent<CameraComponent>();
    cameraComponent.Screen = Screen::Top;
    cameraComponent.Primary = true;
    cameraComponent.Projection = ProjectionType::Perspective;
    cameraComponent.FovDegrees = 60.0f;

    glm::vec3 origin{}, direction{};
    bool ok = scene.ScreenPointToRay3D(Screen::Top, { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f }, origin, direction);

    CHECK(ok);
    CHECK_SOFT(glm::distance(origin, camera.GetComponent<TransformComponent>().Translation) < 0.01f, "ray origin is the camera's own world position");
    // Rotation is {0,0,0} (untouched default) -- forward is straight down -Z, matching this
    // engine's OpenGL -Z-forward convention (see ComposeWorldMtx/OpenGLRenderer3D::BeginScene).
    CHECK_SOFT(glm::distance(direction, glm::vec3(0.0f, 0.0f, -1.0f)) < 0.01f, "screen-center ray points straight down the camera's own forward axis");
}

TEST_CASE("ScreenPointToRay3D fails gracefully with no primary camera or an Orthographic one") {
    Scene scene;
    glm::vec3 origin{}, direction{};
    CHECK_SOFT(!scene.ScreenPointToRay3D(Screen::Bottom, { 0.0f, 0.0f }, origin, direction), "no camera on this screen at all");

    Entity orthoCamera = scene.CreateEntity("OrthoCamera");
    auto& cameraComponent = orthoCamera.AddComponent<CameraComponent>();
    cameraComponent.Screen = Screen::Bottom;
    cameraComponent.Primary = true;
    cameraComponent.Projection = ProjectionType::Orthographic; // default, explicit for clarity
    CHECK_SOFT(!scene.ScreenPointToRay3D(Screen::Bottom, { 0.0f, 0.0f }, origin, direction), "an Orthographic primary camera has no perspective ray to give");
}
