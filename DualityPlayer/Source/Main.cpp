// DualityPlayer -- loads the actual Scene.scene authored/saved in
// DualityEditor (packaged into romfs at build time) and renders it through
// the shared RenderScreen pass, on real citro2d screen targets. This is the
// "scene built in the editor actually runs on the device" milestone.
//
// Behaviour scripts and physics now run here too: GameScripts is linked in
// STATICALLY (3DS has no dynamic loading at all, unlike the desktop Editor's
// hot-reload DLL) -- the exact same script source files as the Editor uses,
// just a different CMake link mode (see GameScripts/CMakeLists.txt). There
// is no Play/Stop on a real device: the simulation just runs from launch
// until START is pressed.

#include <3ds.h>
#include <citro3d.h>

#include <fstream>

#include <nlohmann/json.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/N3DS/Citro2DRenderer.h"
#include "DualityEngine/Renderer/N3DS/Citro3DRenderer.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/PhysicsRaycaster.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/ScriptModule.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"
#include "DualityEngine/Scripting/ScriptableObjectModule.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

// Statically linked into this binary by GameScripts (no dllexport on this
// platform -- see ScriptModuleExports.cpp's DE_SCRIPT_EXPORT).
extern "C" void GetScriptFactories(const Duality::ScriptFactoryEntry** outEntries, int* outCount);
extern "C" void GetScriptableObjectFactories(const Duality::ScriptableObjectFactoryEntry** outEntries, int* outCount);

using namespace Duality;

namespace {
    int LoadN3DSAntiAliasingMode() {
        std::ifstream file("romfs:/BuildSettings.json");
        if (!file)
            return 0;
        try {
            nlohmann::json settings;
            file >> settings;
            int mode = settings.value("N3DSAntiAliasing", 0);
            PhysicsUnits::SetRuntimeOverride(settings.value("PPU", settings.value("PixelsPerMeter", 100.0f)), settings.value("Gravity", 9.81f));
            return mode < 0 ? 0 : (mode > 2 ? 2 : mode);
        } catch (const nlohmann::json::parse_error&) {
            return 0;
        }
    }
}

int main(int argc, char* argv[]) {
    gfxInitDefault();
    romfsInit();

    RegisterBuiltinComponents();

    const ScriptFactoryEntry* entries = nullptr;
    int entryCount = 0;
    GetScriptFactories(&entries, &entryCount);
    for (int i = 0; i < entryCount; i++)
        ScriptRegistry::Register(entries[i]);

    const ScriptableObjectFactoryEntry* scriptableObjectEntries = nullptr;
    int scriptableObjectCount = 0;
    GetScriptableObjectFactories(&scriptableObjectEntries, &scriptableObjectCount);
    for (int i = 0; i < scriptableObjectCount; i++)
        ScriptableObjectRegistry::Register(scriptableObjectEntries[i]);

    // C3D_Init/Fini and C3D_FrameBegin/FrameEnd are owned here, not inside Citro2DRenderer --
    // citro2d is itself built on top of citro3d, and this process-global GPU-frame bracket
    // must be entered/exited exactly once per frame regardless of which screens use the 2D
    // (citro2d) vs. 3D (raw citro3d, once added) pipeline that frame. See Citro2DRenderer::
    // Init/BeginFrame's own comments for the full reasoning.
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

    Citro2DRenderer renderer;
    renderer.Init(LoadN3DSAntiAliasingMode());
    Citro3DRenderer renderer3D;
    renderer3D.Init();
    // Both renderers must draw into the SAME physical-screen render targets -- see
    // Citro2DRenderer::GetTarget's own comment for the real "wrong screen goes black" bug this
    // fixes (two independently-created C3D_RenderTargets both registered for the same screen
    // race for display output; only the one registered last actually gets shown).
    renderer3D.SetScreenTargets(renderer.GetTarget(Screen::Top), renderer.GetTarget(Screen::Bottom));
    AudioEngine::Init();

    // Populates the guid->path index from the manifest BuildPipeline::CookAssets baked into
    // romfs at build time -- DualityPlayer has no real filesystem to scan (unlike the Editor's
    // AssetDatabase::Refresh against the desktop project folder), so this is the on-device
    // equivalent. Without it, every AssetRef (sprite textures, PlaySound guids) would resolve
    // to an empty path here even though the exact same scene works fine in the Editor.
    AssetDatabase::LoadManifest("romfs:/AssetManifest.json");

    Scene scene;
    SceneSerializer(scene).Deserialize("romfs:/Scene.scene");
    scene.OnRuntimeStart();

    u64 lastTick = svcGetSystemTick();

    while (aptMainLoop()) {
        hidScanInput();
        u32 heldKeys = hidKeysHeld();
        if (hidKeysDown() & KEY_START)
            break; // return to hbmenu

        InputManager::BeginFrame();
        InputManager::SetKeyState(KeyCode::GamepadA, heldKeys & KEY_A);
        InputManager::SetKeyState(KeyCode::GamepadB, heldKeys & KEY_B);
        InputManager::SetKeyState(KeyCode::GamepadX, heldKeys & KEY_X);
        InputManager::SetKeyState(KeyCode::GamepadY, heldKeys & KEY_Y);
        InputManager::SetKeyState(KeyCode::GamepadL, heldKeys & KEY_L);
        InputManager::SetKeyState(KeyCode::GamepadR, heldKeys & KEY_R);
        InputManager::SetKeyState(KeyCode::GamepadStart, heldKeys & KEY_START);
        InputManager::SetKeyState(KeyCode::GamepadSelect, heldKeys & KEY_SELECT);
        InputManager::SetKeyState(KeyCode::GamepadDPadUp, heldKeys & KEY_DUP);
        InputManager::SetKeyState(KeyCode::GamepadDPadDown, heldKeys & KEY_DDOWN);
        InputManager::SetKeyState(KeyCode::GamepadDPadLeft, heldKeys & KEY_DLEFT);
        InputManager::SetKeyState(KeyCode::GamepadDPadRight, heldKeys & KEY_DRIGHT);

        // Real analog values here (vs. digital +-1 on desktop) -- same
        // script code, meaningfully different but sane behavior on each
        // platform. libctru's circlePosition roughly spans +-156.
        circlePosition circlePad;
        hidCircleRead(&circlePad);
        auto normalizeAxis = [](s16 value) {
            float axis = static_cast<float>(value) / 156.0f;
            return axis < -1.0f ? -1.0f : (axis > 1.0f ? 1.0f : axis);
        };
        InputManager::SetAxis("Horizontal", normalizeAxis(circlePad.dx));
        InputManager::SetAxis("Vertical", -normalizeAxis(circlePad.dy)); // dy is up-positive; Vertical follows this project's Y-down convention

        touchPosition touch;
        hidTouchRead(&touch);
        // The touch panel is physically only the bottom screen -- hidTouchRead's own px/py are
        // already in that screen's native pixel range (0-320/0-240), top-left origin, Y-down,
        // matching this engine's convention with no conversion needed.
        InputManager::SetPointer((heldKeys & KEY_TOUCH) != 0, { static_cast<float>(touch.px), static_cast<float>(touch.py) }, Screen::Bottom);

        AudioEngine::Update();

        // Before OnRuntimeUpdate, not after -- a script polling UIButtonComponent::WasClicked
        // this frame needs this frame's value already computed, same reasoning as Input's own
        // key-state updates above running before gameplay code.
        UpdateUIInteractions(scene);
        UpdatePhysicsRaycasterInteractions(scene);

        u64 now = svcGetSystemTick();
        float deltaTime = static_cast<float>(now - lastTick) / static_cast<float>(SYSCLOCK_ARM11);
        lastTick = now;

        scene.OnRuntimeUpdate(deltaTime);

        // A script-requested SceneManager.LoadScene is a deferred request (see
        // SceneManager.h's own comment) -- safe to act on now, right after
        // OnRuntimeUpdate returns and before this frame renders anything. The
        // requested path resolves against the SAME "romfs:/Assets/" root
        // BuildPipeline::CookAssets already cooks every non-startup-scene file into.
        if (SceneManager::HasPendingLoad()) {
            std::string pendingPath = SceneManager::ConsumePendingLoad();
            scene.OnRuntimeStop();
            renderer.UnloadAllTextures();
            renderer.UnloadAllFonts();
            renderer3D.UnloadAllTextures();
            renderer3D.UnloadAllMeshes();
            scene = Scene();
            SceneSerializer(scene).Deserialize("romfs:/Assets/" + pendingPath);
            scene.OnRuntimeStart();
        }

        renderer.BeginFrame();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        RenderScreen(renderer, renderer3D, scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f });
        RenderScreen(renderer, renderer3D, scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f });
        C3D_FrameEnd(0);
        renderer.EndFrame();
    }

    scene.OnRuntimeStop();
    AudioEngine::Shutdown();
    renderer.Shutdown();
    renderer3D.Shutdown();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}
