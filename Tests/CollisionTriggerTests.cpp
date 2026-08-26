// Phase B (Collision/Trigger events) regression coverage -- ported from this session's own
// ad hoc scratchpad verification (collision_trigger_test.cpp) into a permanent, CI-run suite.
#include "TestFramework.h"

#include <map>
#include <string>

#include <box2d/box2d.h>

#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    struct Events {
        int CollisionEnter = 0, CollisionExit = 0, TriggerEnter = 0, TriggerExit = 0;
        std::string LastOtherName;
    };

    // Looked up by entity name at OnCreate() time, not at AddComponent<BehaviourComponent>()
    // time -- ScriptRegistry's factory only actually runs later, inside
    // Scene::OnRuntimeStart's own creation loop.
    std::map<std::string, Events*>* g_TargetsByName = nullptr;

    class RecorderBehaviour : public Behaviour {
    public:
        Events* Target = nullptr;
        void OnCreate() override { Target = (*g_TargetsByName)[GetComponent<NameComponent>().Name]; }
        void OnCollisionEnter(Entity other) override { Target->CollisionEnter++; Target->LastOtherName = other.GetComponent<NameComponent>().Name; }
        void OnCollisionExit(Entity other) override { Target->CollisionExit++; }
        void OnTriggerEnter(Entity other) override { Target->TriggerEnter++; Target->LastOtherName = other.GetComponent<NameComponent>().Name; }
        void OnTriggerExit(Entity other) override { Target->TriggerExit++; }
    };

    void EnsureRegistered() {
        static bool registered = false;
        if (registered)
            return;
        ScriptRegistry::Register(ScriptFactoryEntry{
            "RecorderBehaviour",
            []() -> Behaviour* { return new RecorderBehaviour(); },
            [](Behaviour* instance) { delete instance; },
        });
        registered = true;
    }

}

TEST_CASE("2D dynamic-vs-dynamic collision fires Enter then Exit on both sides") {
    EnsureRegistered();
    Scene scene;
    Events eventsA, eventsB;
    std::map<std::string, Events*> targets{ { "BallA", &eventsA }, { "BallB", &eventsB } };
    g_TargetsByName = &targets;

    Entity a = scene.CreateEntity("BallA");
    a.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    a.AddComponent<Rigidbody2DComponent>();
    a.AddComponent<CircleCollider2DComponent>().Radius = 10.0f;
    a.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    Entity b = scene.CreateEntity("BallB");
    b.GetComponent<TransformComponent>().Translation = { 5.0f, 0.0f, 0.0f }; // overlapping A already
    b.AddComponent<Rigidbody2DComponent>();
    b.AddComponent<CircleCollider2DComponent>().Radius = 10.0f;
    b.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    scene.OnRuntimeStart();
    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(eventsA.CollisionEnter == 1, "A got OnCollisionEnter once");
    CHECK_SOFT(eventsB.CollisionEnter == 1, "B got OnCollisionEnter once");
    CHECK_SOFT(eventsA.TriggerEnter == 0, "A did NOT get OnTriggerEnter (neither collider is a trigger)");
    CHECK_SOFT(eventsA.LastOtherName == "BallB", "A's OnCollisionEnter received B as `other`");

    // Teleport B far away via its real b2Body directly (editing TransformComponent
    // wouldn't move the already-simulating body) for a deterministic separation instead
    // of waiting on the solver to resolve a deep overlap naturally.
    static_cast<b2Body*>(b.GetComponent<Rigidbody2DComponent>().RuntimeBody)->SetTransform(b2Vec2(10000.0f, 0.0f), 0.0f);
    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(eventsA.CollisionExit == 1, "A got OnCollisionExit once B was teleported away");
    CHECK_SOFT(eventsB.CollisionExit == 1, "B got OnCollisionExit once teleported away");
    scene.OnRuntimeStop();
}

TEST_CASE("2D trigger collider fires Trigger, not Collision") {
    EnsureRegistered();
    Scene scene;
    Events eventsA, eventsB;
    std::map<std::string, Events*> targets{ { "TriggerZone", &eventsA }, { "Mover", &eventsB } };
    g_TargetsByName = &targets;

    Entity zone = scene.CreateEntity("TriggerZone");
    zone.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    zone.AddComponent<Rigidbody2DComponent>().Type = BodyType::Static;
    auto& zoneCollider = zone.AddComponent<BoxCollider2DComponent>();
    zoneCollider.Size = { 20.0f, 20.0f };
    zoneCollider.IsTrigger = true;
    zone.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    Entity mover = scene.CreateEntity("Mover");
    mover.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f }; // already inside the zone
    mover.AddComponent<Rigidbody2DComponent>();
    mover.AddComponent<CircleCollider2DComponent>().Radius = 5.0f;
    mover.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    scene.OnRuntimeStart();
    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(eventsA.TriggerEnter == 1, "Static trigger zone got OnTriggerEnter");
    CHECK_SOFT(eventsB.TriggerEnter == 1, "Mover got OnTriggerEnter");
    CHECK_SOFT(eventsA.CollisionEnter == 0, "Trigger zone did NOT get OnCollisionEnter");
    CHECK_SOFT(eventsB.CollisionEnter == 0, "Mover did NOT get OnCollisionEnter");
    scene.OnRuntimeStop();
}

TEST_CASE("3D collision fires via the manual Bullet manifold diff") {
    EnsureRegistered();
    Scene scene;
    Events eventsA, eventsB;
    std::map<std::string, Events*> targets{ { "Ground3D", &eventsA }, { "Ball3D", &eventsB } };
    g_TargetsByName = &targets;

    Entity ground = scene.CreateEntity("Ground3D");
    ground.GetComponent<TransformComponent>().Translation = { 0.0f, 50.0f, 0.0f };
    ground.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxCollider3DComponent>().Size = { 100.0f, 10.0f, 100.0f };
    ground.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    Entity ball = scene.CreateEntity("Ball3D");
    ball.GetComponent<TransformComponent>().Translation = { 0.0f, 30.0f, 0.0f }; // overlapping ground (top at Y=40)
    ball.AddComponent<Rigidbody3DComponent>();
    ball.AddComponent<SphereCollider3DComponent>().Radius = 15.0f;
    ball.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    scene.OnRuntimeStart();
    bool sawEnter = false;
    for (int i = 0; i < 10 && !sawEnter; i++) {
        scene.OnRuntimeUpdate(1.0f / 60.0f);
        if (eventsB.CollisionEnter > 0) sawEnter = true;
    }
    CHECK_SOFT(sawEnter, "3D ball got OnCollisionEnter against the static ground");
    CHECK_SOFT(eventsA.CollisionEnter >= 1, "3D ground also got OnCollisionEnter");
    CHECK_SOFT(eventsA.TriggerEnter == 0, "3D ground did NOT get OnTriggerEnter (non-trigger colliders)");
    scene.OnRuntimeStop();
}

TEST_CASE("3D trigger fires Trigger, not Collision") {
    EnsureRegistered();
    Scene scene;
    Events eventsA, eventsB;
    std::map<std::string, Events*> targets{ { "TriggerZone3D", &eventsA }, { "Mover3D", &eventsB } };
    g_TargetsByName = &targets;

    Entity zone = scene.CreateEntity("TriggerZone3D");
    zone.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    zone.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    auto& zoneCollider = zone.AddComponent<BoxCollider3DComponent>();
    zoneCollider.Size = { 20.0f, 20.0f, 20.0f };
    zoneCollider.IsTrigger = true;
    zone.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    Entity mover = scene.CreateEntity("Mover3D");
    mover.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    mover.AddComponent<Rigidbody3DComponent>(); // dynamic (default) -- Bullet's broadphase
    // skips static-vs-static pairs entirely, so at least one side here must be dynamic.
    mover.AddComponent<SphereCollider3DComponent>().Radius = 5.0f;
    mover.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    scene.OnRuntimeStart();
    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(eventsA.TriggerEnter == 1, "3D trigger zone got OnTriggerEnter");
    CHECK_SOFT(eventsB.TriggerEnter == 1, "3D mover got OnTriggerEnter");
    CHECK_SOFT(eventsA.CollisionEnter == 0, "3D trigger zone did NOT get OnCollisionEnter");
    scene.OnRuntimeStop();
}

TEST_CASE("A non-touching pair never fires any collision/trigger event") {
    EnsureRegistered();
    Scene scene;
    Events eventsA, eventsB;
    std::map<std::string, Events*> targets{ { "FarA", &eventsA }, { "FarB", &eventsB } };
    g_TargetsByName = &targets;

    Entity a = scene.CreateEntity("FarA");
    a.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
    a.AddComponent<Rigidbody2DComponent>().Type = BodyType::Static;
    a.AddComponent<CircleCollider2DComponent>().Radius = 5.0f;
    a.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    Entity b = scene.CreateEntity("FarB");
    b.GetComponent<TransformComponent>().Translation = { 10000.0f, 0.0f, 0.0f }; // nowhere near A
    b.AddComponent<Rigidbody2DComponent>().Type = BodyType::Static;
    b.AddComponent<CircleCollider2DComponent>().Radius = 5.0f;
    b.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RecorderBehaviour" });

    scene.OnRuntimeStart();
    for (int i = 0; i < 10; i++)
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(eventsA.CollisionEnter == 0 && eventsA.TriggerEnter == 0, "far-apart pair fired nothing");
    scene.OnRuntimeStop();
}
