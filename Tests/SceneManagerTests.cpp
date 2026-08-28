// SceneManager (deferred scene-load request) regression coverage. The actual Stop/swap/
// Deserialize/Start dance lives in each real entry point's own main loop (DualityPlayer,
// DualityPlayerDesktop, Application::Run), not in SceneManager itself -- SceneManager is
// purely a request mailbox, so these tests only exercise that mailbox contract plus a
// script calling SceneManager::RequestLoadScene during Play (same path Behaviour scripts use).
#include "TestFramework.h"

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    class LoadSceneBehaviour : public Behaviour {
    public:
        void OnUpdate(float) override { SceneManager::RequestLoadScene("Scenes/Level2.scene"); }
    };

    void EnsureRegistered() {
        static bool registered = false;
        if (registered)
            return;
        ScriptRegistry::Register(ScriptFactoryEntry{
            "LoadSceneBehaviour",
            []() -> Behaviour* { return new LoadSceneBehaviour(); },
            [](Behaviour* instance) { delete instance; },
        });
        registered = true;
    }

}

TEST_CASE("SceneManager starts with no pending load") {
    // Other test cases in this same binary may have left a pending request behind
    // (SceneManager's state is process-global) -- consume it first so this test's own
    // checks aren't order-dependent on which TEST_CASE ran before it.
    if (SceneManager::HasPendingLoad())
        SceneManager::ConsumePendingLoad();
    CHECK_SOFT(!SceneManager::HasPendingLoad(), "no pending load before anything requests one");
}

TEST_CASE("RequestLoadScene sets a pending load, ConsumePendingLoad clears it") {
    SceneManager::RequestLoadScene("Scenes/Level2.scene");
    CHECK_SOFT(SceneManager::HasPendingLoad(), "a pending load is now set");
    std::string path = SceneManager::ConsumePendingLoad();
    CHECK_SOFT(path == "Scenes/Level2.scene", "ConsumePendingLoad returns the exact requested path");
    CHECK_SOFT(!SceneManager::HasPendingLoad(), "pending flag cleared after consuming");
}

TEST_CASE("Script SceneManager::RequestLoadScene works from a Behaviour OnUpdate") {
    EnsureRegistered();
    if (SceneManager::HasPendingLoad())
        SceneManager::ConsumePendingLoad();

    Scene scene;
    Entity e = scene.CreateEntity("Loader");
    e.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "LoadSceneBehaviour" });

    scene.OnRuntimeStart();
    scene.OnRuntimeUpdate(1.0f / 60.0f); // OnUpdate calls SceneManager::RequestLoadScene
    scene.OnRuntimeStop();

    CHECK_SOFT(SceneManager::HasPendingLoad(), "a script's scene-load call left a pending request");
    CHECK_SOFT(SceneManager::ConsumePendingLoad() == "Scenes/Level2.scene", "the pending path matches what the script requested");
}
