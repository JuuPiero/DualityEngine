// DualityPlayer -- loads the actual Scene.json authored/saved in
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

#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/N3DS/Citro2DRenderer.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/ScriptModule.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

// Statically linked into this binary by GameScripts (no dllexport on this
// platform -- see ScriptModuleExports.cpp's DE_SCRIPT_EXPORT).
extern "C" void GetScriptFactories(const Duality::ScriptFactoryEntry** outEntries, int* outCount);

using namespace Duality;

int main(int argc, char* argv[]) {
    gfxInitDefault();
    romfsInit();

    RegisterBuiltinComponents();

    const ScriptFactoryEntry* entries = nullptr;
    int entryCount = 0;
    GetScriptFactories(&entries, &entryCount);
    for (int i = 0; i < entryCount; i++)
        ScriptRegistry::Register(entries[i]);

    Citro2DRenderer renderer;
    renderer.Init();
    AudioEngine::Init();

    Scene scene;
    SceneSerializer(scene).Deserialize("romfs:/Scene.json");
    scene.OnRuntimeStart();

    u64 lastTick = svcGetSystemTick();

    while (aptMainLoop()) {
        hidScanInput();
        u32 heldKeys = hidKeysHeld();
        if (hidKeysDown() & KEY_START)
            break; // return to hbmenu

        Input::BeginFrame();
        Input::SetKeyState(KeyCode::GamepadA, heldKeys & KEY_A);
        Input::SetKeyState(KeyCode::GamepadB, heldKeys & KEY_B);
        Input::SetKeyState(KeyCode::GamepadX, heldKeys & KEY_X);
        Input::SetKeyState(KeyCode::GamepadY, heldKeys & KEY_Y);
        Input::SetKeyState(KeyCode::GamepadL, heldKeys & KEY_L);
        Input::SetKeyState(KeyCode::GamepadR, heldKeys & KEY_R);
        Input::SetKeyState(KeyCode::GamepadStart, heldKeys & KEY_START);
        Input::SetKeyState(KeyCode::GamepadSelect, heldKeys & KEY_SELECT);
        Input::SetKeyState(KeyCode::GamepadDPadUp, heldKeys & KEY_DUP);
        Input::SetKeyState(KeyCode::GamepadDPadDown, heldKeys & KEY_DDOWN);
        Input::SetKeyState(KeyCode::GamepadDPadLeft, heldKeys & KEY_DLEFT);
        Input::SetKeyState(KeyCode::GamepadDPadRight, heldKeys & KEY_DRIGHT);

        // Real analog values here (vs. digital +-1 on desktop) -- same
        // script code, meaningfully different but sane behavior on each
        // platform. libctru's circlePosition roughly spans +-156.
        circlePosition circlePad;
        hidCircleRead(&circlePad);
        auto normalizeAxis = [](s16 value) {
            float axis = static_cast<float>(value) / 156.0f;
            return axis < -1.0f ? -1.0f : (axis > 1.0f ? 1.0f : axis);
        };
        Input::SetAxis("Horizontal", normalizeAxis(circlePad.dx));
        Input::SetAxis("Vertical", -normalizeAxis(circlePad.dy)); // dy is up-positive; Vertical follows this project's Y-down convention

        touchPosition touch;
        hidTouchRead(&touch);
        Input::SetPointer((heldKeys & KEY_TOUCH) != 0, { static_cast<float>(touch.px), static_cast<float>(touch.py) });

        AudioEngine::Update();

        u64 now = svcGetSystemTick();
        float deltaTime = static_cast<float>(now - lastTick) / static_cast<float>(SYSCLOCK_ARM11);
        lastTick = now;

        scene.OnRuntimeUpdate(deltaTime);

        renderer.BeginFrame();
        RenderScreen(renderer, scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f });
        RenderScreen(renderer, scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f });
        renderer.EndFrame();
    }

    scene.OnRuntimeStop();
    AudioEngine::Shutdown();
    renderer.Shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
