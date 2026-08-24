// SceneManager (deferred scene-load request) regression coverage. The actual Stop/swap/
// Deserialize/Start dance lives in each real entry point's own main loop (DualityPlayer,
// DualityPlayerDesktop, Application::Run), not in SceneManager itself -- SceneManager is
// purely a request mailbox, so these tests only exercise that mailbox contract plus the
// Behaviour::LoadScene -> EngineServices::RequestLoadScene bridge that feeds it.
#include "TestFramework.h"

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    class LoadSceneBehaviour : public Behaviour {
    public:
        void OnUpdate(float) override { LoadScene("Scenes/Level2.json"); }
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
    SceneManager::RequestLoadScene("Scenes/Level2.json");
    CHECK_SOFT(SceneManager::HasPendingLoad(), "a pending load is now set");
    std::string path = SceneManager::ConsumePendingLoad();
    CHECK_SOFT(path == "Scenes/Level2.json", "ConsumePendingLoad returns the exact requested path");
    CHECK_SOFT(!SceneManager::HasPendingLoad(), "pending flag cleared after consuming");
}

TEST_CASE("Behaviour::LoadScene forwards through EngineServices into SceneManager") {
    EnsureRegistered();
    if (SceneManager::HasPendingLoad())
        SceneManager::ConsumePendingLoad();

    Scene scene;
    Entity e = scene.CreateEntity("Loader");
    e.AddComponent<BehaviourComponent>().ClassName = "LoadSceneBehaviour";

    scene.OnRuntimeStart();
    scene.OnRuntimeUpdate(1.0f / 60.0f); // OnUpdate calls LoadScene("Scenes/Level2.json")
    scene.OnRuntimeStop();

    CHECK_SOFT(SceneManager::HasPendingLoad(), "a script's LoadScene() call left a pending request");
    CHECK_SOFT(SceneManager::ConsumePendingLoad() == "Scenes/Level2.json", "the pending path matches what the script requested");
}
