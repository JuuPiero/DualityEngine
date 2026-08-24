// Phase A (Active/Enable-Disable) regression coverage -- ported from this session's own
// ad hoc scratchpad verification (active_enable_test.cpp) into a permanent, CI-run suite.
#include "TestFramework.h"

#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    struct Counters {
        int OnCreate = 0, OnUpdate = 0, OnEnable = 0, OnDisable = 0, OnDestroy = 0;
    };
    Counters* g_Counters = nullptr;

    class CountingBehaviour : public Behaviour {
    public:
        void OnCreate() override { g_Counters->OnCreate++; }
        void OnEnable() override { g_Counters->OnEnable++; }
        void OnUpdate(float) override { g_Counters->OnUpdate++; }
        void OnDisable() override { g_Counters->OnDisable++; }
        void OnDestroy() override { g_Counters->OnDestroy++; }
    };

    void EnsureRegistered() {
        static bool registered = false;
        if (registered)
            return;
        ScriptRegistry::Register(ScriptFactoryEntry{
            "CountingBehaviour",
            []() -> Behaviour* { return new CountingBehaviour(); },
            [](Behaviour* instance) { delete instance; },
        });
        registered = true;
    }

}

TEST_CASE("ActiveComponent cascades through the parent chain") {
    Scene scene;
    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, parent);

    CHECK_SOFT(scene.IsEffectivelyActive(child), "child active while parent active (both default true)");
    parent.GetComponent<ActiveComponent>().Active = false;
    CHECK_SOFT(!scene.IsEffectivelyActive(child), "child effectively inactive when parent is inactive");
    CHECK_SOFT(child.GetComponent<ActiveComponent>().Active, "child's OWN Active flag is untouched by parent's state");
    parent.GetComponent<ActiveComponent>().Active = true;
    CHECK_SOFT(scene.IsEffectivelyActive(child), "child active again once parent re-enabled");
}

TEST_CASE("OnEnable/OnDisable fire exactly once per transition") {
    EnsureRegistered();
    Scene scene;
    Counters counters;
    g_Counters = &counters;

    Entity e = scene.CreateEntity("Scripted");
    e.AddComponent<BehaviourComponent>().ClassName = "CountingBehaviour";

    scene.OnRuntimeStart();
    CHECK(counters.OnCreate == 1);
    CHECK_SOFT(counters.OnEnable == 0, "OnEnable not yet fired at OnRuntimeStart itself");

    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(counters.OnEnable == 1, "OnEnable fired exactly once on the first tick");
    CHECK_SOFT(counters.OnUpdate == 1, "OnUpdate ran on that same first tick");

    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(counters.OnEnable == 1, "OnEnable still only fired once after a second normal tick");
    CHECK_SOFT(counters.OnUpdate == 2, "OnUpdate ran again");

    auto* instance = static_cast<CountingBehaviour*>(e.GetComponent<BehaviourComponent>().Instance);
    instance->SetActive(false);
    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(counters.OnDisable == 1, "OnDisable fired exactly once right after SetActive(false)");
    CHECK_SOFT(counters.OnUpdate == 2, "OnUpdate did NOT run while inactive");

    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(counters.OnDisable == 1, "OnDisable did not re-fire on a second inactive tick");

    instance->SetActive(true);
    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(counters.OnEnable == 2, "OnEnable fired again exactly once after reactivating");
    CHECK_SOFT(counters.OnUpdate == 3, "OnUpdate resumed");

    scene.OnRuntimeStop();
    CHECK_SOFT(counters.OnDisable == 2, "final OnDisable fired at OnRuntimeStop since it was still active");
    CHECK_SOFT(counters.OnDestroy == 1, "OnDestroy fired once, after that final OnDisable");
}

TEST_CASE("An entity that starts inactive never gets OnEnable/OnDisable") {
    EnsureRegistered();
    Scene scene;
    Counters counters;
    g_Counters = &counters;

    Entity e = scene.CreateEntity("NeverActive");
    e.GetComponent<ActiveComponent>().Active = false;
    e.AddComponent<BehaviourComponent>().ClassName = "CountingBehaviour";

    scene.OnRuntimeStart();
    CHECK_SOFT(counters.OnCreate == 1, "OnCreate still fires even for an entity that starts inactive");

    scene.OnRuntimeUpdate(1.0f / 60.0f);
    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(counters.OnEnable == 0, "OnEnable never fires for an entity that starts and stays inactive");
    CHECK_SOFT(counters.OnUpdate == 0, "OnUpdate never runs either");

    scene.OnRuntimeStop();
    CHECK_SOFT(counters.OnDisable == 0, "OnDisable does NOT fire -- it was never enabled to begin with");
    CHECK_SOFT(counters.OnDestroy == 1, "OnDestroy still fires regardless");
}
