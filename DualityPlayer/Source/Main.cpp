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
#include <citro3d.h>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/N3DS/Citro2DRenderer.h"
#include "DualityEngine/Renderer/N3DS/Citro3DRenderer.h"
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

    // C3D_Init/Fini and C3D_FrameBegin/FrameEnd are owned here, not inside Citro2DRenderer --
    // citro2d is itself built on top of citro3d, and this process-global GPU-frame bracket
    // must be entered/exited exactly once per frame regardless of which screens use the 2D
    // (citro2d) vs. 3D (raw citro3d, once added) pipeline that frame. See Citro2DRenderer::
    // Init/BeginFrame's own comments for the full reasoning.
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

    Citro2DRenderer renderer;
    renderer.Init();
    Citro3DRenderer renderer3D;
    renderer3D.Init();
    AudioEngine::Init();

    // Populates the guid->path index from the manifest BuildPipeline::CookAssets baked into
    // romfs at build time -- DualityPlayer has no real filesystem to scan (unlike the Editor's
    // AssetDatabase::Refresh against the desktop project folder), so this is the on-device
    // equivalent. Without it, every AssetRef (sprite textures, PlaySound guids) would resolve
    // to an empty path here even though the exact same scene works fine in the Editor.
    AssetDatabase::LoadManifest("romfs:/AssetManifest.json");

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
