// SceneManager (deferred scene-load request) regression coverage. The actual Stop/swap/
// Deserialize/Start dance lives in each real entry point's own main loop (DualityPlayer,
// DualityPlayerDesktop, Application::Run), not in SceneManager itself -- SceneManager is
// purely a request mailbox, so these tests only exercise that mailbox contract plus a
// Behaviour calling ScriptScene during Play (the same EngineServices path GameScripts use).
#include "TestFramework.h"

#include <deque>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scripting/ScriptScene.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    class LoadSceneBehaviour : public Behaviour {
    public:
        void OnUpdate(float) override { ScriptScene::LoadSceneAdditive("Scenes/Level2.scene"); }
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
    SceneManager::ClearPendingRequests();
    CHECK_SOFT(!SceneManager::HasPendingLoad(), "no pending load before anything requests one");
}

TEST_CASE("RequestLoadScene sets a pending load, ConsumePendingLoad clears it") {
    SceneManager::ClearPendingRequests();
    SceneManager::RequestLoadScene("Scenes/Level2.scene");
    CHECK_SOFT(SceneManager::HasPendingLoad(), "a pending load is now set");
    std::string path = SceneManager::ConsumePendingLoad();
    CHECK_SOFT(path == "Scenes/Level2.scene", "ConsumePendingLoad returns the exact requested path");
    CHECK_SOFT(!SceneManager::HasPendingLoad(), "pending flag cleared after consuming");
}

TEST_CASE("ScriptScene additive load works from a Behaviour OnUpdate") {
    EnsureRegistered();
    SceneManager::ClearPendingRequests();

    Scene scene;
    Entity e = scene.CreateEntity("Loader");
    e.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "LoadSceneBehaviour" });

    scene.OnRuntimeStart();
    scene.OnRuntimeUpdate(1.0f / 60.0f); // OnUpdate calls SceneManager::RequestLoadScene
    scene.OnRuntimeStop();

    CHECK_SOFT(SceneManager::HasPendingLoad(), "a script's scene-load call left a pending request");
    std::deque<SceneRequest> requests = SceneManager::ConsumePendingRequests();
    CHECK_SOFT(requests.size() == 1, "the script produced exactly one request");
    CHECK_SOFT(requests.front().Path == "Scenes/Level2.scene", "the pending path matches what the script requested");
    CHECK_SOFT(requests.front().Mode == LoadSceneMode::Additive, "the script's additive mode survives the DLL-safe bridge");
}

TEST_CASE("SceneManager preserves ordered Single Additive and Unload requests") {
    SceneManager::ClearPendingRequests();
    SceneManager::RequestLoadScene("Scenes/Base.scene");
    SceneManager::RequestLoadSceneAdditive("Scenes/Hud.scene");
    SceneManager::RequestUnloadScene("Scenes/Hud.scene");

    std::deque<SceneRequest> requests = SceneManager::ConsumePendingRequests();
    CHECK_SOFT(requests.size() == 3, "none of several same-frame requests is overwritten");
    CHECK_SOFT(requests[0].Type == SceneRequestType::Load && requests[0].Mode == LoadSceneMode::Single && requests[0].Path == "Scenes/Base.scene", "Single request remains first");
    CHECK_SOFT(requests[1].Type == SceneRequestType::Load && requests[1].Mode == LoadSceneMode::Additive && requests[1].Path == "Scenes/Hud.scene", "Additive request retains its mode and order");
    CHECK_SOFT(requests[2].Type == SceneRequestType::Unload && requests[2].Path == "Scenes/Hud.scene", "Unload request retains its target and order");
    CHECK_SOFT(!SceneManager::HasPendingRequests(), "consuming the queue empties it completely");
}
