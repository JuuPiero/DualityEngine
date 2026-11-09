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

#include <algorithm>
#include <fstream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/N3DS/Citro2DRenderer.h"
#include "DualityEngine/Renderer/N3DS/Citro3DRenderer.h"
#include "DualityEngine/Renderer/RenderSettings.h"
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
#include "DualityEngine/Scripting/JS/JsScripting.h"
#include "DualityEngine/Core/Log.h"

#include <cstdio>

// Statically linked into this binary by GameScripts (no dllexport on this
// platform -- see ScriptModuleExports.cpp's DE_SCRIPT_EXPORT).
extern "C" void GetScriptFactories(const Duality::ScriptFactoryEntry** outEntries, int* outCount);
extern "C" void GetScriptableObjectFactories(const Duality::ScriptableObjectFactoryEntry** outEntries, int* outCount);

// Main-thread stack (libctru's crt0 reads this weak symbol; its default is only 32 KB). The
// QuickJS interpreter recurses on the native stack for every nested JS call, so TypeScript
// scripts need real headroom: 512 KB here, of which JsRuntimeOptions lets JS use 192 KB
// before it throws a catchable "stack overflow" instead of corrupting memory.
extern "C" { u32 __stacksize__ = 512 * 1024; }

using namespace Duality;

namespace {
    // APT invokes this from the platform side. It never calls scripts or
    // renderer/audio code directly: the main loop drains these tiny requests
    // and forwards portable lifecycle callbacks through Scene instead.
    struct N3DSLifecycleRequests {
        bool SuspendRequested = false;
        bool ResumeRequested = false;
        bool ExitRequested = false;
    };

    void N3DSAptHook(APT_HookType hook, void* parameter) {
        auto* requests = static_cast<N3DSLifecycleRequests*>(parameter);
        switch (hook) {
            case APTHOOK_ONSUSPEND:
            case APTHOOK_ONSLEEP:
                requests->SuspendRequested = true;
                break;
            case APTHOOK_ONRESTORE:
            case APTHOOK_ONWAKEUP:
                requests->ResumeRequested = true;
                break;
            case APTHOOK_ONEXIT:
                requests->ExitRequested = true;
                break;
            default:
                break;
        }
    }

    // Optional "PerfLog": true in BuildSettings.json: every 120 frames append GPU/CPU timings
    // and Citro3DRenderer counters to sdmc:/duality_perf.txt (off by default, costs nothing).
    bool s_PerfLog = false;

    void AppendPerfLog(u32 frame, const Citro3DFrameStats& stats) {
        FILE* file = fopen("sdmc:/duality_perf.txt", "a");
        if (!file)
            return;
        fprintf(file, "frame %lu: cpu %.2f ms, gpu %.2f ms, cmdbuf %.1f%%, draws %lu (lit %lu), verts %lu (lit %lu), light slot updates %lu, material LUT changes %lu, program binds %lu\n",
            static_cast<unsigned long>(frame), C3D_GetProcessingTime(), C3D_GetDrawingTime(), C3D_GetCmdBufUsage() * 100.0f,
            static_cast<unsigned long>(stats.DrawCalls), static_cast<unsigned long>(stats.LitDrawCalls),
            static_cast<unsigned long>(stats.Vertices), static_cast<unsigned long>(stats.LitVertices),
            static_cast<unsigned long>(stats.LightSlotUpdates), static_cast<unsigned long>(stats.MaterialLutChanges),
            static_cast<unsigned long>(stats.ProgramBinds));
        fclose(file);
    }

    int LoadN3DSBuildSettings() {
        std::ifstream file("romfs:/BuildSettings.json");
        if (!file)
            return 0;
        try {
            nlohmann::json settings;
            file >> settings;
            int mode = settings.value("N3DSAntiAliasing", 0);
            PhysicsUnits::SetRuntimeOverride(settings.value("PPU", settings.value("PixelsPerMeter", 100.0f)), settings.value("Gravity", 9.81f));
            RenderSettings::SetShadowMode(ShadowModeFromInt(settings.value("ShadowTechnique", static_cast<int>(ShadowMode::BlobShadows))));
            s_PerfLog = settings.value("PerfLog", false);
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
    renderer.Init(LoadN3DSBuildSettings());
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

    // TypeScript scripts (BuildPipeline::CookTypeScript): QuickJS bytecode when DualityJsc was
    // available at build time (no parse cost/parser memory at boot), else the minified bundle.
    {
        const u64 loadStart = svcGetSystemTick();
        bool loaded = false;
        if (FILE* bytecode = std::fopen("romfs:/Scripts/scripts.jsbc", "rb")) {
            std::fclose(bytecode);
            loaded = JsScripting::LoadBundle("romfs:/Scripts/scripts.jsbc");
        }
        if (!loaded) {
            if (FILE* source = std::fopen("romfs:/Scripts/scripts.js", "rb")) {
                std::fclose(source);
                loaded = JsScripting::LoadBundle("romfs:/Scripts/scripts.js");
            }
        }
        if (loaded) {
            const double ms = static_cast<double>(svcGetSystemTick() - loadStart) * 1000.0 / static_cast<double>(SYSCLOCK_ARM11);
            Log::Info("DualityPlayer: TypeScript ready in " + std::to_string(ms) + " ms, JS heap " +
                std::to_string(JsScripting::GetMemoryStats().MallocSize / 1024) + " KB");
        }
    }

    Scene scene;
    SceneSerializer(scene).Deserialize("romfs:/Scene.scene");
    scene.OnRuntimeStart();

    // Additive worlds stay independent from the base scene. They are updated and rendered in
    // load order, after the base scene, so a gameplay scene can keep a HUD/transition scene
    // alive without serializing both into one file.
    struct AdditiveScene {
        std::string Path;
        std::unique_ptr<Scene> Value;
    };
    std::string activeScenePath;
    std::vector<AdditiveScene> additiveScenes;
    auto stopAdditiveScenes = [&]() {
        for (auto& loaded : additiveScenes)
            loaded.Value->OnRuntimeStop();
        additiveScenes.clear();
    };

    N3DSLifecycleRequests lifecycleRequests;
    aptHookCookie lifecycleHook{};
    aptHook(&lifecycleHook, N3DSAptHook, &lifecycleRequests);
    bool applicationPaused = false;
    bool applicationFocused = true;

    u64 lastTick = svcGetSystemTick();

    while (aptMainLoop() && !lifecycleRequests.ExitRequested) {
        // APT can issue suspend/sleep and restore/wake while graphics or
        // network services are unavailable. Drain only on this safe main-loop
        // boundary; callback code can then save state or request reconnects.
        if (lifecycleRequests.SuspendRequested) {
            lifecycleRequests.SuspendRequested = false;
            if (applicationFocused) {
                scene.OnApplicationFocus(false);
                applicationFocused = false;
            }
            if (!applicationPaused) {
                scene.OnApplicationPause(true);
                applicationPaused = true;
            }
        }
        if (lifecycleRequests.ResumeRequested) {
            lifecycleRequests.ResumeRequested = false;
            if (applicationPaused) {
                scene.OnApplicationPause(false);
                applicationPaused = false;
            }
            if (!applicationFocused) {
                scene.OnApplicationFocus(true);
                applicationFocused = true;
            }
        }

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
        for (auto& loaded : additiveScenes) {
            UpdateUIInteractions(*loaded.Value);
            UpdatePhysicsRaycasterInteractions(*loaded.Value);
        }

        u64 now = svcGetSystemTick();
        float deltaTime = static_cast<float>(now - lastTick) / static_cast<float>(SYSCLOCK_ARM11);
        lastTick = now;

        const u64 updateStart = svcGetSystemTick();
        scene.OnRuntimeUpdate(deltaTime);
        for (auto& loaded : additiveScenes)
            loaded.Value->OnRuntimeUpdate(deltaTime);
        // Script + physics cost (no rendering), reported every 300 frames while TS is loaded.
        if (JsScripting::IsLoaded()) {
            static u64 updateTicks = 0;
            static u32 updateFrames = 0;
            updateTicks += svcGetSystemTick() - updateStart;
            if (++updateFrames == 300) {
                const double ms = static_cast<double>(updateTicks) * 1000.0 / static_cast<double>(SYSCLOCK_ARM11) / updateFrames;
                Log::Info("DualityPlayer: Scene::OnRuntimeUpdate avg " + std::to_string(ms) + " ms over 300 frames, JS heap " +
                    std::to_string(JsScripting::GetMemoryStats().MallocSize / 1024) + " KB");
                updateTicks = 0;
                updateFrames = 0;
            }
        }

        // Deferred queue processing keeps scene destruction outside every Behaviour callback.
        // The build cooker keeps non-start scenes at romfs:/Assets/, so both Single and
        // Additive requests resolve through the same root.
        for (const SceneRequest& request : SceneManager::ConsumePendingRequests()) {
            if (request.Type == SceneRequestType::Unload) {
                auto found = std::find_if(additiveScenes.begin(), additiveScenes.end(), [&](const AdditiveScene& loaded) { return loaded.Path == request.Path; });
                if (found != additiveScenes.end()) {
                    found->Value->OnRuntimeStop();
                    additiveScenes.erase(found);
                }
                continue;
            }

            if (request.Mode == LoadSceneMode::Single) {
                stopAdditiveScenes();
                scene.OnRuntimeStop();
                renderer.UnloadAllTextures();
                renderer.UnloadAllFonts();
                renderer3D.UnloadAllTextures();
                renderer3D.UnloadAllMeshes();
                scene = Scene();
                if (SceneSerializer(scene).Deserialize("romfs:/Assets/" + request.Path)) {
                    activeScenePath = request.Path;
                    scene.OnRuntimeStart();
                }
                continue;
            }

            const bool alreadyLoaded = request.Path == activeScenePath || std::any_of(additiveScenes.begin(), additiveScenes.end(), [&](const AdditiveScene& loaded) { return loaded.Path == request.Path; });
            if (alreadyLoaded)
                continue;
            auto loaded = std::make_unique<Scene>();
            if (!SceneSerializer(*loaded).Deserialize("romfs:/Assets/" + request.Path))
                continue;
            loaded->OnRuntimeStart();
            additiveScenes.push_back({ request.Path, std::move(loaded) });
        }

        renderer.BeginFrame();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        RenderScreen(renderer, renderer3D, scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f }, true);
        for (auto& loaded : additiveScenes)
            RenderScreen(renderer, renderer3D, *loaded.Value, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f }, false);
        RenderScreen(renderer, renderer3D, scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f }, true);
        for (auto& loaded : additiveScenes)
            RenderScreen(renderer, renderer3D, *loaded.Value, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f }, false);
        C3D_FrameEnd(0);
        renderer.EndFrame();
        if (s_PerfLog) {
            static u32 perfFrame = 0;
            if (++perfFrame % 120 == 0)
                AppendPerfLog(perfFrame, renderer3D.GetFrameStats());
            renderer3D.ResetFrameStats();
        }
    }

    aptUnhook(&lifecycleHook);
    for (auto& loaded : additiveScenes)
        loaded.Value->OnApplicationQuit();
    scene.OnApplicationQuit();
    stopAdditiveScenes();
    scene.OnRuntimeStop();
    JsScripting::Unload();
    AudioEngine::Shutdown();
    renderer.Shutdown();
    renderer3D.Shutdown();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}
