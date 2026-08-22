// DualityPlayer -- loads the actual Scene.json authored/saved in
// DualityEditor (packaged into romfs at build time) and renders it through
// the shared RenderScreen pass, on real citro2d screen targets. This is the
// "scene built in the editor actually runs on the device" milestone --
// previously this target only ever drew a hardcoded demo scene.
//
// Behaviour scripts (BehaviourComponent) don't run on-device yet -- that
// needs GameScripts compiled statically into this binary, which is a later
// phase; ScriptRegistry is simply empty here, so Scene::OnRuntimeStart logs
// "unknown script class" for any entity that has one and otherwise no-ops.

#include <3ds.h>

#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/N3DS/Citro2DRenderer.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;

int main(int argc, char* argv[]) {
    gfxInitDefault();
    romfsInit();

    RegisterBuiltinComponents();

    Citro2DRenderer renderer;
    renderer.Init();

    Scene scene;
    SceneSerializer(scene).Deserialize("romfs:/Scene.json");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
            break; // return to hbmenu

        renderer.BeginFrame();
        RenderScreen(renderer, scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f });
        RenderScreen(renderer, scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f });
        renderer.EndFrame();
    }

    renderer.Shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
