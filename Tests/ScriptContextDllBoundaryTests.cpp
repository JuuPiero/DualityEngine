// Regression coverage for a real, severe bug found and fixed this session: ScriptContext's
// per-callback state (Bind/Clear/Services/Scene/EntityHandle) used to live in its own
// `inline static` header variables -- which get an independent, always-default-initialized copy
// in EACH binary that includes the header on Windows/MinGW (no automatic cross-DLL
// deduplication). Scene::OnRuntimeUpdate's Bind() call, compiled into the host EXE/
// DualityEngine.lib, was silently writing to a completely different memory location than
// Input::GetAxis()/ScriptPhysics2D::Raycast()/ScriptScene::FindEntityInScreen(), compiled
// into the separately-linked GameScripts.dll on desktop -- every one of those calls silently
// returned its "nothing bound" default in every desktop Play/DualityPlayerDesktop session, while
// working correctly on the 3DS build (GameScripts links statically there, so there's only one
// copy of everything). Fixed by moving the genuinely-per-callback state (CurrentScene/
// CurrentEntityHandle) onto the one shared EngineServices instance itself (a pointer dereference
// reads/writes the same memory regardless of which binary's code performs it) and by making
// Behaviour::SetEngineServices virtual so its self-healing EnsureBound call actually executes
// from GameScripts.dll's own vtable-dispatched code, not whichever binary happens to call it.
//
// This test only means anything if it actually loads the real GameScripts.dll (the same one the
// Editor/DualityPlayerDesktop load) -- unlike every other Tests/*.cpp file, which never touches
// GameScripts at all (see GameScripts/CMakeLists.txt's own comment on why Tests doesn't link it).
#include "TestFramework.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/ScriptModule.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {
    // This test binary's own build directory (e.g. ".../build/Tests/DualityEngineTests.exe" ->
    // ".../build") -- matches Application.cpp's GetBuildDirectory(), used here so the paths
    // below work regardless of where this repo is checked out, not just on the machine this
    // test was originally written on.
    std::string GetBuildDirectory() {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string exePath = path;
        size_t pos = exePath.find_last_of("\\/");
        exePath = exePath.substr(0, pos); // strip "DualityEngineTests.exe"
        pos = exePath.find_last_of("\\/");
        exePath = exePath.substr(0, pos); // strip "Tests"
        return exePath;
    }

    bool LoadGameScripts() {
        std::string path = GetBuildDirectory() + "/lib/GameScripts.dll";
        HMODULE module = LoadLibraryA(path.c_str());
        if (!module) {
            Log::Error("ScriptContextDllBoundaryTests: could not load '" + path + "'");
            return false;
        }
        auto getFactories = reinterpret_cast<GetScriptFactoriesFn>(GetProcAddress(module, "GetScriptFactories"));
        if (!getFactories) {
            Log::Error("ScriptContextDllBoundaryTests: GetScriptFactories export not found");
            return false;
        }
        const ScriptFactoryEntry* entries = nullptr;
        int count = 0;
        getFactories(&entries, &count);
        for (int i = 0; i < count; i++)
            ScriptRegistry::Register(entries[i]);
        return count > 0;
    }

    std::string PlatformerScenePath() {
        std::string buildDir = GetBuildDirectory();
        std::string repoRoot = buildDir.substr(0, buildDir.find_last_of("\\/")); // parent of "build"
        return repoRoot + "/SampleProject/Assets/Scenes/Platformer.scene";
    }
}

TEST_CASE("A real GameScripts.dll script reads Input/UIButton state across the DLL boundary") {
    CHECK(LoadGameScripts());

    Scene scene;
    CHECK(SceneSerializer(scene).Deserialize(PlatformerScenePath()));

    Entity player = scene.FindEntityInScreen(Screen::Top, "Player");
    CHECK(static_cast<bool>(player));

    scene.OnRuntimeStart();

    auto& scripts = player.GetComponent<BehaviourComponent>().Scripts;
    CHECK_SOFT(!scripts.empty() && scripts[0].Instance != nullptr, "PlayerController instance was actually created");
    CHECK_SOFT(player.GetComponent<Rigidbody2DComponent>().RuntimeBody != nullptr, "Player has a real Box2D body after OnRuntimeStart");

    float startX = player.GetComponent<TransformComponent>().Translation.x;
    float startY = player.GetComponent<TransformComponent>().Translation.y;

    // Baseline, independent of scripts/ScriptContext entirely: does gravity alone (a passive
    // Box2D force) move the player with zero simulated input? Confirms the body is really being
    // stepped before blaming ScriptContext for anything.
    for (int i = 0; i < 10; i++) {
        InputManager::BeginFrame();
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    CHECK_SOFT(player.GetComponent<TransformComponent>().Translation.y != startY, "Player fell under gravity alone with zero input");

    // Simulate holding the Right key -- exercises Input::GetAxis, which routes through
    // ScriptContext::Services(), called from PlayerController::OnUpdate (GameScripts.dll code).
    for (int i = 0; i < 30; i++) {
        InputManager::BeginFrame();
        InputManager::SetAxis("Horizontal", 1.0f);
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    CHECK_SOFT(player.GetComponent<TransformComponent>().Translation.x > startX + 5.0f,
        "Player moved right in response to a simulated Horizontal axis (Input::GetAxis actually works across the DLL boundary)");

    // Reset axis, simulate a RightButton UI click held for 30 frames -- exercises
    // ScriptScene::FindEntityInScreen (used in OnCreate to resolve RightButton by name) and the
    // UIButton wrapper, bypassing GamePanel's own pointer-to-screen mapping entirely.
    InputManager::SetAxis("Horizontal", 0.0f);
    Entity rightButton = scene.FindEntityInScreen(Screen::Bottom, "RightButton");
    CHECK(static_cast<bool>(rightButton));
    float xBeforeButton = player.GetComponent<TransformComponent>().Translation.x;
    for (int i = 0; i < 30; i++) {
        InputManager::BeginFrame();
        rightButton.GetComponent<UIButtonComponent>().IsPressed = true;
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    CHECK_SOFT(player.GetComponent<TransformComponent>().Translation.x > xBeforeButton + 5.0f,
        "Player moved right in response to a simulated RightButton press (ScriptScene::FindEntityInScreen resolved it correctly in OnCreate)");

    scene.OnRuntimeStop();
}

TEST_CASE("A grounded player actually jumps (IsGrounded's raycast + Space key work end to end)") {
    CHECK(LoadGameScripts());

    Scene scene;
    CHECK(SceneSerializer(scene).Deserialize(PlatformerScenePath()));
    Entity player = scene.FindEntityInScreen(Screen::Top, "Player");
    CHECK(static_cast<bool>(player));

    scene.OnRuntimeStart();

    // Let gravity settle the player onto Ground1 first -- IsGrounded's raycast only succeeds
    // once actually resting on a platform, same as any real play session.
    for (int i = 0; i < 60; i++) {
        InputManager::BeginFrame();
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    float restingY = player.GetComponent<TransformComponent>().Translation.y;

    // A few more settled frames -- Y should now be essentially stable (resting on Ground1), not
    // still falling, confirming the player actually landed before the jump itself is tested.
    for (int i = 0; i < 5; i++) {
        InputManager::BeginFrame();
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    float stableY = player.GetComponent<TransformComponent>().Translation.y;
    CHECK_SOFT(std::fabs(stableY - restingY) < 1.0f, "player has actually come to rest on Ground1 before the jump is attempted");

    // Raw ground-check probe, same geometry PlayerController::IsGrounded uses (origin at the
    // player's own CENTER, cast past its own feet by a small margin) -- via the public Scene::
    // Raycast2D API directly, to isolate whether the RAYCAST GEOMETRY itself finds the ground,
    // independent of anything ScriptPhysics2D/ScriptContext-related.
    {
        auto& collider = player.GetComponent<BoxCollider2DComponent>();
        glm::vec2 origin{ player.GetComponent<TransformComponent>().Translation.x, stableY };
        RaycastHit2D hit = scene.Raycast2D(origin, { 0.0f, 1.0f }, collider.Size.y + 4.0f);
        CHECK_SOFT(static_cast<bool>(hit), "raw Scene::Raycast2D (same geometry as IsGrounded) finds the ground while resting on it");
        if (hit)
            Log::Info("JumpTest: ground raycast hit '" + (hit.HitEntity.HasComponent<NameComponent>() ? hit.HitEntity.GetComponent<NameComponent>().Name : std::string("?")) + "' at distance " + std::to_string(hit.Distance));
        else
            Log::Info("JumpTest: ground raycast found NOTHING");
    }

    // GetKeyDown fires on the rising edge only -- BeginFrame must see Space as NOT down for at
    // least one prior frame, then down for exactly this one.
    InputManager::BeginFrame();
    InputManager::SetKeyState(KeyCode::Space, false);
    scene.OnRuntimeUpdate(1.0f / 60.0f);

    InputManager::BeginFrame();
    InputManager::SetKeyState(KeyCode::Space, true);
    scene.OnRuntimeUpdate(1.0f / 60.0f);

    InputManager::SetKeyState(KeyCode::Space, false);
    for (int i = 0; i < 5; i++) {
        InputManager::BeginFrame();
        scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    float yAfterJump = player.GetComponent<TransformComponent>().Translation.y;
    CHECK_SOFT(yAfterJump < stableY - 5.0f, "player rose (Y decreased, world +Y is down) after a simulated Space press while grounded");
    Log::Info("JumpTest: stableY=" + std::to_string(stableY) + " yAfterJump=" + std::to_string(yAfterJump));

    scene.OnRuntimeStop();
}
