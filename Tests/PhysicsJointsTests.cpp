// Behaviour of the physics joints / effectors / Character Controller in stepped simulations:
// Edge Collider 2D, the 2D joints (+ break / OnJointBreak2D), the 2D effectors and Constant
// Force 2D (Box2D), and the 3D joints, Mesh Collider, Character Controller and Constant Force
// (Bullet). +Y is DOWN in this engine (gravity is +9.81 on Y).
#include "TestFramework.h"

#include <cmath>
#include <cstdio>
#include <string>

#include <box2d/box2d.h>

#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Scene/PhysicsSystems.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    constexpr float Dt = 1.0f / 60.0f;

    struct ExtEvents {
        int JointBreak2D = 0;
        int JointBreak3D = 0;
        int ControllerHits = 0;
        float LastBreakForce = 0.0f;
        std::string LastHitName;
    };
    ExtEvents g_Events;

    class ExtRecorder : public Behaviour {
    public:
        void OnJointBreak2D(Entity, float force) override { g_Events.JointBreak2D++; g_Events.LastBreakForce = force; }
        void OnJointBreak(Entity, float force) override { g_Events.JointBreak3D++; g_Events.LastBreakForce = force; }
        void OnControllerColliderHit(const ControllerColliderHit& hit) override {
            g_Events.ControllerHits++;
            if (hit.Other)
                g_Events.LastHitName = hit.Other.GetComponent<NameComponent>().Name;
        }
    };

    void EnsureRecorder() {
        static bool registered = false;
        if (registered)
            return;
        ScriptRegistry::Register(ScriptFactoryEntry{
            "ExtRecorder",
            []() -> Behaviour* { return new ExtRecorder(); },
            [](Behaviour* instance) { delete instance; },
        });
        registered = true;
    }

    // Gravity is read when a scene starts Play: scope it to zero for weightless test scenes.
    struct ZeroGravity {
        ZeroGravity() { PhysicsUnits::SetRuntimeOverride(PhysicsUnits::DefaultPPU, 0.0f); }
        ~ZeroGravity() { PhysicsUnits::SetRuntimeOverride(PhysicsUnits::DefaultPPU, PhysicsUnits::DefaultGravity); }
    };

    void Step(Scene& scene, int frames) {
        for (int i = 0; i < frames; ++i)
            scene.OnRuntimeUpdate(Dt);
    }

    Entity Body2D(Scene& scene, const char* name, glm::vec2 position, float radius, BodyType type = BodyType::Dynamic) {
        Entity e = scene.CreateEntity(name);
        e.GetComponent<TransformComponent>().Translation = { position.x, position.y, 0.0f };
        e.AddComponent<Rigidbody2DComponent>().Type = type;
        e.AddComponent<CircleCollider2DComponent>().Radius = radius;
        return e;
    }

    Entity Box2DStatic(Scene& scene, const char* name, glm::vec2 position, glm::vec2 halfSize) {
        Entity e = scene.CreateEntity(name);
        e.GetComponent<TransformComponent>().Translation = { position.x, position.y, 0.0f };
        e.AddComponent<BoxCollider2DComponent>().Size = halfSize;
        return e;
    }

    glm::vec2 Pos2(Entity e) { return glm::vec2(e.GetComponent<TransformComponent>().Translation); }
    glm::vec3 Pos3(Entity e) { return e.GetComponent<TransformComponent>().Translation; }

    EntityRef Ref(Entity e) { return EntityRef{ static_cast<uint32_t>(e.Handle()) }; }

    Entity Box3D(Scene& scene, const char* name, glm::vec3 position, glm::vec3 halfSize, bool dynamic) {
        Entity e = scene.CreateEntity(name);
        e.GetComponent<TransformComponent>().Translation = position;
        e.AddComponent<BoxCollider3DComponent>().Size = halfSize;
        if (dynamic)
            e.AddComponent<Rigidbody3DComponent>();
        return e;
    }
}

// ----------------------------------------------------------------------------------- 2D

TEST_CASE("Edge Collider 2D: a ball lands and rests on a static polyline") {
    Scene scene;
    Entity ground = scene.CreateEntity("Ground");
    ground.GetComponent<TransformComponent>().Translation = { 0.0f, 1.0f, 0.0f };
    auto& edge = ground.AddComponent<EdgeCollider2DComponent>();
    edge.PointCount = 3;
    edge.Point0 = { -2.0f, 0.0f };
    edge.Point1 = { 0.0f, 0.0f };
    edge.Point2 = { 2.0f, 0.0f };
    Entity ball = Body2D(scene, "Ball", { 0.3f, 0.0f }, 0.1f);

    scene.OnRuntimeStart();
    CHECK(edge.RuntimeFixture != nullptr);
    Step(scene, 150);
    CHECK_SOFT(std::fabs(Pos2(ball).y - 0.9f) < 0.03f, "ball rests one radius above the edge (y = 0.9)");
    scene.OnRuntimeStop();
    CHECK_SOFT(edge.RuntimeFixture == nullptr, "runtime fixture cleared at stop");
}

TEST_CASE("Hinge Joint 2D: pendulum keeps its radius; motor spins at Motor Speed") {
    Scene scene;
    Entity bob = Body2D(scene, "Bob", { 1.0f, 0.0f }, 0.05f);
    auto& hinge = bob.AddComponent<HingeJoint2DComponent>();
    hinge.Anchor = { -1.0f, 0.0f }; // world pivot at the origin

    Scene motorScene;
    Entity wheel = Body2D(motorScene, "Wheel", { 0.0f, 0.0f }, 0.2f);
    wheel.GetComponent<Rigidbody2DComponent>().GravityScale = 0.0f;
    auto& motor = wheel.AddComponent<HingeJoint2DComponent>();
    motor.UseMotor = true;
    motor.MotorSpeed = 90.0f;
    motor.MaxMotorTorque = 1000.0f;

    scene.OnRuntimeStart();
    motorScene.OnRuntimeStart();
    CHECK(hinge.RuntimeJoint != nullptr);
    Step(scene, 40);
    Step(motorScene, 60);
    CHECK_SOFT(std::fabs(glm::length(Pos2(bob)) - 1.0f) < 0.02f, "bob stays 1 unit from the pivot");
    CHECK_SOFT(Pos2(bob).y > 0.3f, "bob swung down (+Y)");
    const float angle = wheel.GetComponent<TransformComponent>().Rotation.z;
    CHECK_SOFT(std::fabs(angle - 90.0f) < 6.0f, "motor turned ~90 degrees in 1 s");
    CHECK_SOFT(std::fabs(motor.JointSpeed - 90.0f) < 2.0f, "JointSpeed readout in degrees/second");
    scene.OnRuntimeStop();
    motorScene.OnRuntimeStop();
}

TEST_CASE("Distance / Spring / Slider / Target / Friction joints 2D behave like Unity's") {
    Scene scene;
    Entity ceiling = Body2D(scene, "Ceiling", { 0.0f, 0.0f }, 0.05f, BodyType::Static);
    Entity rope = Body2D(scene, "Rope", { 0.5f, 0.0f }, 0.05f);
    auto& distance = rope.AddComponent<DistanceJoint2DComponent>();
    distance.ConnectedBody = Ref(ceiling);

    Entity spring = Body2D(scene, "Spring", { 3.0f, 0.5f }, 0.05f);
    auto& springJoint = spring.AddComponent<SpringJoint2DComponent>();
    springJoint.Anchor = { 0.0f, -0.5f }; // world point (3, 0)
    springJoint.Frequency = 2.0f;
    springJoint.DampingRatio = 0.5f;

    Entity slider = Body2D(scene, "Slider", { 6.0f, 0.0f }, 0.05f);
    auto& sliderJoint = slider.AddComponent<SliderJoint2DComponent>();
    sliderJoint.Angle = 0.0f; // horizontal rail: gravity cannot move it

    Entity dragged = Body2D(scene, "Dragged", { 9.0f, 0.0f }, 0.05f);
    dragged.GetComponent<Rigidbody2DComponent>().GravityScale = 0.0f;
    auto& target = dragged.AddComponent<TargetJoint2DComponent>();
    target.AutoConfigureTarget = false;
    target.Target = { 10.0f, 0.0f };

    Entity sliding = Body2D(scene, "Sliding", { 12.0f, 0.0f }, 0.05f);
    sliding.GetComponent<Rigidbody2DComponent>().GravityScale = 0.0f;
    auto& friction = sliding.AddComponent<FrictionJoint2DComponent>();
    friction.MaxForce = 100.0f;

    scene.OnRuntimeStart();
    CHECK(distance.RuntimeJoint && springJoint.RuntimeJoint && sliderJoint.RuntimeJoint && target.RuntimeJoint && friction.RuntimeJoint);
    CHECK_SOFT(std::fabs(distance.Distance - 0.5f) < 1e-4f, "Auto Configure Distance measured the start distance");
    // Kick the friction-jointed body: the joint must eat the motion quickly.
    static_cast<b2Body*>(sliding.GetComponent<Rigidbody2DComponent>().RuntimeBody)->SetLinearVelocity(b2Vec2(3.0f, 0.0f));
    Step(scene, 1);
    Step(scene, 119);
    CHECK_SOFT(std::fabs(glm::length(Pos2(rope)) - 0.5f) < 0.02f, "rope length kept");
    const float springLength = glm::length(Pos2(spring) - glm::vec2(3.0f, 0.0f));
    CHECK_SOFT(springLength > 0.5f, "spring stretched under gravity");
    CHECK_SOFT(springLength < 2.0f, "spring holds the body");
    CHECK_SOFT(std::fabs(Pos2(slider).y) < 0.01f && std::fabs(Pos2(slider).x - 6.0f) < 0.01f, "slider stays on its horizontal rail");
    CHECK_SOFT(std::fabs(Pos2(dragged).x - 10.0f) < 0.05f, "target joint pulled the body to Target");
    CHECK_SOFT(Pos2(sliding).x < 12.5f, "friction joint stopped the kicked body");
    scene.OnRuntimeStop();
}

TEST_CASE("Joint 2D Break Force: joint is destroyed, OnJointBreak2D fires once, component removed") {
    EnsureRecorder();
    g_Events = {};
    Scene scene;
    Entity heavy = Body2D(scene, "Heavy", { 0.0f, 0.0f }, 0.2f);
    auto& weld = heavy.AddComponent<FixedJoint2DComponent>();
    weld.BreakForce = 0.05f; // gravity alone exceeds it
    heavy.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "ExtRecorder" });
    Entity anchored = Body2D(scene, "Anchored", { 2.0f, 0.0f }, 0.2f);
    anchored.AddComponent<FixedJoint2DComponent>(); // unbreakable (0 = infinity)

    scene.OnRuntimeStart();
    Step(scene, 30);
    CHECK_SOFT(g_Events.JointBreak2D == 1, "OnJointBreak2D called once");
    CHECK_SOFT(g_Events.LastBreakForce > 0.05f, "break force reported");
    CHECK_SOFT(!heavy.HasComponent<FixedJoint2DComponent>(), "broken joint component removed (Unity destroys it)");
    CHECK_SOFT(Pos2(heavy).y > 0.3f, "body falls once free");
    CHECK_SOFT(std::fabs(Pos2(anchored).y) < 0.01f, "unbreakable weld still holds");
    scene.OnRuntimeStop();
}

TEST_CASE("Platform Effector 2D: one-way -- jump through from below, land on top") {
    Scene scene;
    Entity platform = Box2DStatic(scene, "Platform", { 0.0f, 0.0f }, { 1.0f, 0.05f });
    platform.GetComponent<BoxCollider2DComponent>().UsedByEffector = true;
    platform.AddComponent<PlatformEffector2DComponent>();
    Entity ball = Body2D(scene, "Ball", { 0.0f, 0.5f }, 0.1f);

    scene.OnRuntimeStart();
    auto* body = static_cast<b2Body*>(ball.GetComponent<Rigidbody2DComponent>().RuntimeBody);
    CHECK(body);
    body->SetLinearVelocity(b2Vec2(0.0f, -6.0f)); // upwards (-Y)
    float highest = 1.0f;
    for (int i = 0; i < 240; ++i) {
        scene.OnRuntimeUpdate(Dt);
        highest = std::min(highest, Pos2(ball).y);
    }
    CHECK_SOFT(highest < -0.5f, "ball passed up through the platform");
    CHECK_SOFT(std::fabs(Pos2(ball).y - (-0.15f)) < 0.03f, "ball rests ON the platform (y = -0.05 - 0.1)");
    scene.OnRuntimeStop();
}

TEST_CASE("Buoyancy / Area / Point / Surface effectors and Constant Force 2D") {
    Scene scene;
    Scene space; // weightless bodies (wind, magnet, rocket)
    // Water: a 2 x 2 trigger volume, surface at its top edge (y = -1).
    Entity water = Box2DStatic(scene, "Water", { 0.0f, 0.0f }, { 1.0f, 1.0f });
    water.GetComponent<BoxCollider2DComponent>().IsTrigger = true;
    auto& buoyancy = water.AddComponent<BuoyancyEffector2DComponent>();
    buoyancy.SurfaceLevel = 1.0f;
    buoyancy.Density = 2.0f;
    buoyancy.LinearDrag = 6.0f;
    Entity crate = scene.CreateEntity("Crate");
    crate.GetComponent<TransformComponent>().Translation = { 0.0f, -1.5f, 0.0f };
    crate.AddComponent<Rigidbody2DComponent>().FixedRotation = true;
    crate.AddComponent<BoxCollider2DComponent>().Size = { 0.1f, 0.1f };

    // Wind: area effector pushing +X on a weightless body.
    Entity wind = Box2DStatic(space, "Wind", { 5.0f, 0.0f }, { 1.0f, 1.0f });
    wind.GetComponent<BoxCollider2DComponent>().IsTrigger = true;
    wind.AddComponent<AreaEffector2DComponent>().ForceMagnitude = 1.0f;
    Entity leaf = Body2D(space, "Leaf", { 5.0f, 0.0f }, 0.05f);
    leaf.GetComponent<Rigidbody2DComponent>().GravityScale = 0.0f;

    // Magnet: point effector attracting.
    Entity magnet = space.CreateEntity("Magnet");
    magnet.GetComponent<TransformComponent>().Translation = { 10.0f, 10.0f, 0.0f };
    auto& field = magnet.AddComponent<CircleCollider2DComponent>();
    field.Radius = 1.0f;
    field.IsTrigger = true;
    auto& pull = magnet.AddComponent<PointEffector2DComponent>();
    pull.ForceMagnitude = -0.02f;
    pull.ForceMode = EffectorForceMode2D::Constant;
    Entity coin = Body2D(space, "Coin", { 10.6f, 10.0f }, 0.05f);
    coin.GetComponent<Rigidbody2DComponent>().GravityScale = 0.0f;

    // Conveyor: surface effector under a box.
    Entity belt = Box2DStatic(scene, "Belt", { 15.0f, 0.0f }, { 3.0f, 0.1f });
    belt.AddComponent<SurfaceEffector2DComponent>().Speed = 1.0f;
    Entity parcel = scene.CreateEntity("Parcel");
    parcel.GetComponent<TransformComponent>().Translation = { 15.0f, -0.2f, 0.0f };
    parcel.AddComponent<Rigidbody2DComponent>().FixedRotation = true;
    parcel.AddComponent<BoxCollider2DComponent>().Size = { 0.1f, 0.1f };

    // Rocket: constant force.
    Entity rocket = Body2D(space, "Rocket", { 20.0f, 20.0f }, 0.1f);
    rocket.GetComponent<Rigidbody2DComponent>().GravityScale = 0.0f;
    rocket.AddComponent<ConstantForce2DComponent>().Force = { 0.5f, 0.0f };

    scene.OnRuntimeStart();
    {
        ZeroGravity weightless; // the wind / magnet / rocket scene has no gravity
        space.OnRuntimeStart();
    }
    const float rocketMass = static_cast<b2Body*>(rocket.GetComponent<Rigidbody2DComponent>().RuntimeBody)->GetMass();
    Step(scene, 60);
    Step(space, 60);
    const float rocketSpeed = static_cast<b2Body*>(rocket.GetComponent<Rigidbody2DComponent>().RuntimeBody)->GetLinearVelocity().x;
    CHECK_SOFT(std::fabs(rocketSpeed - 0.5f / rocketMass) < 0.05f * (0.5f / rocketMass), "Constant Force 2D: v = F t / m after 1 s");
    CHECK_SOFT(Pos2(leaf).x > 5.05f, "Area Effector pushed the leaf +X");
    CHECK_SOFT(Pos2(coin).x < 10.5f, "Point Effector (negative) attracted the coin");
    Step(scene, 240);
    const float crateY = Pos2(crate).y;
    CHECK_SOFT(crateY > -1.2f && crateY < -0.8f, "crate floats at the surface (water half-submerged at density 2)");
    const auto* crateBody = static_cast<b2Body*>(crate.GetComponent<Rigidbody2DComponent>().RuntimeBody);
    CHECK_SOFT(std::fabs(crateBody->GetLinearVelocity().y) < 0.2f, "crate settled (drag)");
    const auto* parcelBody = static_cast<b2Body*>(parcel.GetComponent<Rigidbody2DComponent>().RuntimeBody);
    CHECK_SOFT(std::fabs(parcelBody->GetLinearVelocity().x - 1.0f) < 0.15f, "Surface Effector moves the parcel at Speed");
    space.OnRuntimeStop();
    scene.OnRuntimeStop();
}

// ----------------------------------------------------------------------------------- 3D

TEST_CASE("Hinge Joint 3D: a motorized door turns about its hinge; Fixed Joint holds") {
    Scene scene;
    Entity door = Box3D(scene, "Door", { 0.5f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.05f }, true);
    door.GetComponent<Rigidbody3DComponent>().UseGravity = false;
    auto& hinge = door.AddComponent<HingeJointComponent>();
    hinge.Anchor = { -0.5f, 0.0f, 0.0f }; // world hinge line through the origin
    hinge.Axis = { 0.0f, 1.0f, 0.0f };
    hinge.UseMotor = true;
    hinge.MotorTargetVelocity = 90.0f;
    hinge.MotorForce = 50.0f;

    Entity hanging = Box3D(scene, "Hanging", { 3.0f, 0.0f, 0.0f }, { 0.1f, 0.1f, 0.1f }, true);
    hanging.AddComponent<FixedJointComponent>();

    scene.OnRuntimeStart();
    CHECK(hinge.RuntimeConstraint != nullptr);
    Step(scene, 60);
    const glm::vec3 p = Pos3(door);
    CHECK_SOFT(std::fabs(glm::length(glm::vec2(p.x, p.z)) - 0.5f) < 0.03f, "door center stays 0.5 from the hinge line");
    CHECK_SOFT(std::fabs(p.y) < 0.02f, "door does not leave the hinge plane");
    // +90 degrees about +Y (right-hand rule) takes (0.5,0,0) to (0,0,-0.5).
    CHECK_SOFT(std::fabs(p.x) < 0.1f && p.z < -0.4f, "motor turned the door +90 degrees about +Y");
    CHECK_SOFT(std::fabs(hinge.Angle - 90.0f) < 8.0f, "Angle readout ~ +90");
    CHECK_SOFT(glm::length(Pos3(hanging) - glm::vec3(3.0f, 0.0f, 0.0f)) < 0.02f, "Fixed Joint keeps the body in place under gravity");
    scene.OnRuntimeStop();
}

TEST_CASE("Spring Joint 3D hangs a body; Joint 3D Break Force fires OnJointBreak") {
    EnsureRecorder();
    g_Events = {};
    Scene scene;
    Entity weight = Box3D(scene, "Weight", { 0.0f, 0.5f, 0.0f }, { 0.1f, 0.1f, 0.1f }, true);
    auto& spring = weight.AddComponent<SpringJointComponent>();
    spring.Anchor = { 0.0f, -0.5f, 0.0f }; // world anchor (0,0,0); rest length 0.5
    spring.Spring = 2.0f;
    spring.Damper = 0.2f;

    Entity fragile = Box3D(scene, "Fragile", { 5.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f }, true);
    fragile.AddComponent<FixedJointComponent>().BreakForce = 0.01f;
    fragile.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "ExtRecorder" });

    scene.OnRuntimeStart();
    Step(scene, 300);
    const float y = Pos3(weight).y;
    CHECK_SOFT(y > 0.5f && y < 1.5f, "spring stretched past its rest length but holds the weight");
    CHECK_SOFT(g_Events.JointBreak3D == 1, "OnJointBreak fired once");
    CHECK_SOFT(!fragile.HasComponent<FixedJointComponent>(), "broken joint removed");
    CHECK_SOFT(Pos3(fragile).y > 1.0f, "freed body falls");
    scene.OnRuntimeStop();
}

TEST_CASE("Mesh Collider: static triangle mesh floor and dynamic convex hull") {
    Scene scene;
    Entity floor = scene.CreateEntity("Floor");
    floor.GetComponent<TransformComponent>().Translation = { 0.0f, 1.0f, 0.0f };
    floor.GetComponent<TransformComponent>().Scale = { 10.0f, 1.0f, 10.0f };
    floor.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Plane);
    floor.AddComponent<MeshRendererComponent>();
    floor.AddComponent<MeshColliderComponent>();

    Entity ball = scene.CreateEntity("Ball");
    ball.AddComponent<Rigidbody3DComponent>();
    ball.AddComponent<SphereCollider3DComponent>().Radius = 0.1f;

    Entity rock = scene.CreateEntity("Rock");
    rock.GetComponent<TransformComponent>().Translation = { 2.0f, 0.0f, 0.0f };
    rock.GetComponent<TransformComponent>().Scale = { 0.2f, 0.2f, 0.2f };
    rock.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Cube);
    rock.AddComponent<MeshRendererComponent>();
    rock.AddComponent<Rigidbody3DComponent>();
    rock.AddComponent<MeshColliderComponent>().Convex = true;

    scene.OnRuntimeStart();
    Step(scene, 180);
    CHECK_SOFT(std::fabs(Pos3(ball).y - 0.9f) < 0.03f, "sphere rests on the mesh plane");
    CHECK_SOFT(Pos3(rock).y > 0.75f && Pos3(rock).y < 0.95f, "convex cube rests on the plane");
    scene.OnRuntimeStop();
}

TEST_CASE("Character Controller: gravity + ground, walls, steps, OnControllerColliderHit") {
    EnsureRecorder();
    g_Events = {};
    Scene scene;
    Box3D(scene, "Ground", { 0.0f, 1.0f, 0.0f }, { 5.0f, 0.5f, 5.0f }, false);   // top face at y = 0.5
    Box3D(scene, "Wall", { 3.0f, 0.0f, 0.0f }, { 0.1f, 2.0f, 2.0f }, false);     // face at x = 2.9
    Box3D(scene, "Step", { 1.5f, 0.4f, 0.0f }, { 0.5f, 0.1f, 2.0f }, false);     // top at y = 0.3 (0.2 high)
    Entity player = scene.CreateEntity("Player");
    player.GetComponent<TransformComponent>().Translation = { 0.0f, -0.5f, 0.0f };
    auto& cc = player.AddComponent<CharacterControllerComponent>();
    cc.Radius = 0.25f;
    cc.Height = 1.0f;
    cc.StepOffset = 0.3f;
    player.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "ExtRecorder" });

    scene.OnRuntimeStart();
    CHECK(cc.RuntimeObject != nullptr);
    for (int i = 0; i < 90; ++i) {
        scene.OnRuntimeUpdate(Dt);
        PhysicsSystems::CharacterSimpleMove(scene, player, glm::vec3(0.0f));
    }
    CHECK_SOFT(cc.IsGrounded, "grounded after falling");
    const float groundY = 0.5f - 0.5f - cc.SkinWidth;
    CHECK_SOFT(std::fabs(Pos3(player).y - groundY) < 0.04f, "rests Skin Width above the ground");

    // Walk +X: climb the 0.2 step, then stop at the wall.
    for (int i = 0; i < 120; ++i) {
        scene.OnRuntimeUpdate(Dt);
        PhysicsSystems::CharacterSimpleMove(scene, player, glm::vec3(2.0f, 0.0f, 0.0f));
    }
    const glm::vec3 p = Pos3(player);
    CHECK_SOFT(p.x > 2.5f && p.x < 2.9f - 0.25f + 0.01f, "stopped by the wall (x <= 2.9 - radius)");
    CHECK_SOFT(cc.IsGrounded, "still grounded while pushing the wall");
    const int flags = PhysicsSystems::CharacterMove(scene, player, glm::vec3(0.5f, 0.0f, 0.0f));
    CHECK_SOFT((flags & CollisionSides) != 0, "Move into the wall reports CollisionSides");
    CHECK_SOFT(g_Events.ControllerHits > 0, "OnControllerColliderHit fired");

    // Step test from the other side: walking back over the step climbs it.
    float highestOnStep = 10.0f;
    for (int i = 0; i < 60; ++i) {
        scene.OnRuntimeUpdate(Dt);
        PhysicsSystems::CharacterSimpleMove(scene, player, glm::vec3(-1.0f, 0.0f, 0.0f));
        if (std::fabs(Pos3(player).x - 1.5f) < 0.2f)
            highestOnStep = std::min(highestOnStep, Pos3(player).y);
    }
    CHECK_SOFT(highestOnStep < groundY - 0.15f, "climbed the 0.2 step (Step Offset 0.3)");
    scene.OnRuntimeStop();
    CHECK_SOFT(cc.RuntimeObject == nullptr, "controller object released at stop");
}

TEST_CASE("Constant Force 3D accelerates a body") {
    Scene scene;
    Entity body = Box3D(scene, "Body", { 0.0f, 0.0f, 0.0f }, { 0.1f, 0.1f, 0.1f }, true);
    body.GetComponent<Rigidbody3DComponent>().UseGravity = false;
    body.AddComponent<ConstantForceComponent>().Force = { 0.0f, 0.0f, 0.01f };
    scene.OnRuntimeStart();
    Step(scene, 60);
    CHECK_SOFT(Pos3(body).z > 0.01f, "moved along +Z");
    scene.OnRuntimeStop();
}
