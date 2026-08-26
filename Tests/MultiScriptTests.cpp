// Multiple scripts per entity (BehaviourComponent::Scripts, a vector of ScriptInstance slots
// instead of one ClassName/Instance pair) regression coverage.
#include "TestFramework.h"

#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    struct Counters {
        int OnCreate = 0, OnUpdate = 0, OnDestroy = 0;
    };
    Counters* g_CountersA = nullptr;
    Counters* g_CountersB = nullptr;

    class ScriptA : public Behaviour {
    public:
        void OnCreate() override { g_CountersA->OnCreate++; }
        void OnUpdate(float) override { g_CountersA->OnUpdate++; }
        void OnDestroy() override { g_CountersA->OnDestroy++; }
    };

    class ScriptB : public Behaviour {
    public:
        void OnCreate() override { g_CountersB->OnCreate++; }
        void OnUpdate(float) override { g_CountersB->OnUpdate++; }
        void OnDestroy() override { g_CountersB->OnDestroy++; }
    };

    void EnsureRegistered() {
        static bool registered = false;
        if (registered)
            return;
        ScriptRegistry::Register(ScriptFactoryEntry{
            "MultiScriptTests_ScriptA",
            []() -> Behaviour* { return new ScriptA(); },
            [](Behaviour* instance) { delete instance; },
        });
        ScriptRegistry::Register(ScriptFactoryEntry{
            "MultiScriptTests_ScriptB",
            []() -> Behaviour* { return new ScriptB(); },
            [](Behaviour* instance) { delete instance; },
        });
        registered = true;
    }

}

TEST_CASE("Two different scripts on one entity both fire OnCreate/OnUpdate/OnDestroy independently") {
    EnsureRegistered();
    Counters countersA, countersB;
    g_CountersA = &countersA;
    g_CountersB = &countersB;

    Scene scene;
    Entity e = scene.CreateEntity("MultiScripted");
    auto& bc = e.AddComponent<BehaviourComponent>();
    bc.Scripts.push_back(ScriptInstance{ "MultiScriptTests_ScriptA" });
    bc.Scripts.push_back(ScriptInstance{ "MultiScriptTests_ScriptB" });
    CHECK(bc.Scripts.size() == 2);

    scene.OnRuntimeStart();
    CHECK_SOFT(countersA.OnCreate == 1, "ScriptA's own OnCreate fired");
    CHECK_SOFT(countersB.OnCreate == 1, "ScriptB's own OnCreate fired, independently of ScriptA");

    scene.OnRuntimeUpdate(1.0f / 60.0f);
    CHECK_SOFT(countersA.OnUpdate == 1, "ScriptA's own OnUpdate fired");
    CHECK_SOFT(countersB.OnUpdate == 1, "ScriptB's own OnUpdate fired");

    CHECK_SOFT(bc.Scripts[0].Instance != bc.Scripts[1].Instance, "each slot owns a genuinely separate instance");

    scene.OnRuntimeStop();
    CHECK_SOFT(countersA.OnDestroy == 1, "ScriptA's own OnDestroy fired on Stop");
    CHECK_SOFT(countersB.OnDestroy == 1, "ScriptB's own OnDestroy fired on Stop");
}

TEST_CASE("The same script class attached twice to one entity runs as two independent instances") {
    EnsureRegistered();
    Counters countersA;
    g_CountersA = &countersA;
    g_CountersB = &countersA; // unused by this test, just needs to point somewhere valid

    Scene scene;
    Entity e = scene.CreateEntity("DoubleScripted");
    auto& bc = e.AddComponent<BehaviourComponent>();
    bc.Scripts.push_back(ScriptInstance{ "MultiScriptTests_ScriptA" });
    bc.Scripts.push_back(ScriptInstance{ "MultiScriptTests_ScriptA" });

    scene.OnRuntimeStart();
    CHECK_SOFT(countersA.OnCreate == 2, "both slots of the same class each got their own OnCreate");
    CHECK_SOFT(bc.Scripts[0].Instance != bc.Scripts[1].Instance, "two slots of the same class are still two distinct instances");
    scene.OnRuntimeStop();
}
