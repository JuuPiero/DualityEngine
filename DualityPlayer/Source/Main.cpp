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

    Scene scene;
    SceneSerializer(scene).Deserialize("romfs:/Scene.json");
    scene.OnRuntimeStart();

    u64 lastTick = svcGetSystemTick();

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break; // return to hbmenu

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
    renderer.Shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
