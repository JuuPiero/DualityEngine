// DualityPlayerDesktop -- the desktop equivalent of DualityPlayer: loads the same Scene.scene a
// project saves in the Editor and runs it in a real, standalone GLFW window using
// OpenGLRenderer2D/3D, no ImGui/Editor UI involved at all. This is "export the game as a
// Windows .exe", the desktop counterpart to DualityPlayer's "run the game on real 3DS
// hardware" -- same Scene/SceneSerializer/AssetDatabase/GameScripts pipeline either way, just a
// different window + renderer backend, matching how the Editor's own Game panel and
// DualityPlayer already share that exact pipeline via SceneRenderer.cpp's RenderScreen.
//
// GameScripts is a real SHARED DLL on desktop (see GameScripts/CMakeLists.txt) -- linked
// directly here (not LoadLibrary'd at runtime like the Editor's own hot-reloading ScriptEngine,
// since this is a one-shot "run the game" entry point with no reload button).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <utility>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer3D.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/ScriptModule.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"
#include "DualityEngine/Scripting/ScriptableObjectModule.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

// Linked directly from GameScripts.dll's import library (see this project's own
// ScriptModuleExports.cpp / DE_SCRIPT_EXPORT) -- same extern "C" declaration DualityPlayer's
// 3DS Main.cpp uses for its statically-linked equivalent.
extern "C" __declspec(dllimport) void GetScriptFactories(const Duality::ScriptFactoryEntry** outEntries, int* outCount);
extern "C" __declspec(dllimport) void GetScriptableObjectFactories(const Duality::ScriptableObjectFactoryEntry** outEntries, int* outCount);

using namespace Duality;

namespace {
    // Same (KeyCode, GLFW key) table as DualityEditor/Source/Window.cpp -- duplicated rather
    // than shared since Window.cpp is bundled with GLFW+ImGui+docking setup this player has no
    // use for at all, and the table itself is a handful of lines.
    constexpr std::pair<KeyCode, int> kDesktopKeyMap[] = {
        { KeyCode::W, GLFW_KEY_W }, { KeyCode::A, GLFW_KEY_A },
        { KeyCode::S, GLFW_KEY_S }, { KeyCode::D, GLFW_KEY_D },
        { KeyCode::Up, GLFW_KEY_UP }, { KeyCode::Down, GLFW_KEY_DOWN },
        { KeyCode::Left, GLFW_KEY_LEFT }, { KeyCode::Right, GLFW_KEY_RIGHT },
        { KeyCode::Space, GLFW_KEY_SPACE }, { KeyCode::Enter, GLFW_KEY_ENTER },
        { KeyCode::Escape, GLFW_KEY_ESCAPE },
    };

    // 2x the real 400x240/320x240 screens for comfortable viewing on a modern monitor -- purely
    // a display scale, the ortho projection inside OpenGLRenderer2D/3D's own BeginScene doesn't
    // care what the actual viewport pixel size is (same reason the Editor's resizable Game
    // panel already works at arbitrary sizes).
    constexpr int DisplayScale = 2;
    constexpr int WindowWidth = TopScreenWidth * DisplayScale;   // 400 and 320 are the two
    constexpr int WindowHeight = (TopScreenHeight + BottomScreenHeight) * DisplayScale; // screens' own widths/heights (Screen.h)

    // Renders `screen` into its own sub-rectangle of the single real window/default
    // framebuffer -- glViewport alone does NOT confine glClear (a screen's own BeginScene
    // clears the WHOLE currently-bound framebuffer, not just its viewport), so both are also
    // scissor-rectangled to the same region, otherwise each screen's clear would erase the
    // other's already-drawn content. Unlike the Editor (which gives each screen its own
    // offscreen Framebuffer object specifically to avoid this), there's only one real window
    // here, so scissoring is the simpler fix -- no second Framebuffer-like utility needed.
    void RenderScreenIntoViewport(IRenderer2D& renderer2D, IRenderer3D& renderer3D, Scene& scene, Screen screen, const glm::vec4& clearColor, int x, int y, int w, int h) {
        glViewport(x, y, w, h);
        glScissor(x, y, w, h);
        glEnable(GL_SCISSOR_TEST);
        RenderScreen(renderer2D, renderer3D, scene, screen, clearColor);
        glDisable(GL_SCISSOR_TEST);
    }

    // Top occupies the window's top half, Bottom the bottom half (centered horizontally, since
    // it's narrower) -- mirrors how a real 3DS is physically held (top screen above bottom) and
    // how the Editor's own Game panel stacks them vertically.
    struct ScreenViewport { int X, Y, W, H; };
    ScreenViewport TopViewport() { return { 0, TopScreenHeight * DisplayScale, WindowWidth, TopScreenHeight * DisplayScale }; }
    ScreenViewport BottomViewport() {
        int w = BottomScreenWidth * DisplayScale, h = BottomScreenHeight * DisplayScale;
        return { (WindowWidth - w) / 2, 0, w, h };
    }

    // Maps a raw window-space (GLFW, pixels, Y-down from the window's own top-left) mouse
    // position into whichever screen's own logical pixel space it currently falls within --
    // there's only one global Input pointer (see Input::SetPointer), so this picks whichever
    // screen the mouse is "over" this frame, letting either screen's UI/pointer-driven gameplay
    // respond, unlike the fixed touch-is-always-Bottom convention real 3DS hardware has.
    bool MapWindowPointToScreen(double windowX, double windowY, glm::vec2& outScreenPoint) {
        ScreenViewport top = TopViewport(), bottom = BottomViewport();
        // GLFW's own cursor Y is measured from the window's top edge, same direction as the
        // viewports below are measured from the window's BOTTOM edge (OpenGL convention) --
        // flipped once here so both compare in the same sense.
        double flippedY = WindowHeight - windowY;

        if (windowX >= top.X && windowX < top.X + top.W && flippedY >= top.Y && flippedY < top.Y + top.H) {
            outScreenPoint = { (windowX - top.X) / DisplayScale, (WindowHeight - windowY - top.Y) / DisplayScale };
            return true;
        }
        if (windowX >= bottom.X && windowX < bottom.X + bottom.W && flippedY >= bottom.Y && flippedY < bottom.Y + bottom.H) {
            outScreenPoint = { (windowX - bottom.X) / DisplayScale, (WindowHeight - windowY - bottom.Y) / DisplayScale };
            return true;
        }
        return false;
    }

    // Repo root, computed from this executable's own path the same way DualityEditor::
    // Application.cpp's GetBuildDirectory() does -- this .exe sits at
    // "<repo>/build-desktop-player/DualityPlayerDesktop/DualityPlayerDesktop.exe" (or
    // whatever the build directory is named), so stripping the exe name and its containing
    // folder twice recovers the build directory, and once more the repo root.
    std::string GetRepoRoot() {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string exePath = path;
        exePath = exePath.substr(0, exePath.find_last_of("\\/")); // strip "DualityPlayerDesktop.exe"
        exePath = exePath.substr(0, exePath.find_last_of("\\/")); // strip "DualityPlayerDesktop"
        exePath = exePath.substr(0, exePath.find_last_of("\\/")); // strip the build directory itself
        return exePath;
    }
}

int main() {
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

    if (!glfwInit()) {
        Log::Error("DualityPlayerDesktop: glfwInit failed");
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(WindowWidth, WindowHeight, "DualityEngine", nullptr, nullptr);
    if (!window) {
        Log::Error("DualityPlayerDesktop: glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        Log::Error("DualityPlayerDesktop: glewInit failed");
        glfwTerminate();
        return 1;
    }

    OpenGLRenderer2D renderer;
    renderer.Init();
    OpenGLRenderer3D renderer3D;
    renderer3D.Init();
    AudioEngine::Init();

    // Same project/scene the Editor itself opens by default -- a real "export" build would let
    // the user pick which project to bundle; this first pass always runs SampleProject, same
    // convention as DualityPlayer's own romfs/Scene.scene being a manually-copied snapshot for
    // now (see that file's own CMakeLists.txt comment).
    std::string repoRoot = GetRepoRoot();
    std::shared_ptr<Project> project = Project::Load(repoRoot + "/SampleProject/SampleProject.dproj");
    if (!project) {
        Log::Error("DualityPlayerDesktop: could not load SampleProject -- expected it next to the repo root (" + repoRoot + ")");
        return 1;
    }
    AssetDatabase::Refresh(project->GetAssetsDirectory());

    Scene scene;
    SceneSerializer(scene).Deserialize(project->GetAssetsDirectory() + "/Scene.scene");
    scene.OnRuntimeStart();

    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            break;

        Input::BeginFrame();
        for (auto& [keyCode, glfwKey] : kDesktopKeyMap)
            Input::SetKeyState(keyCode, glfwGetKey(window, glfwKey) == GLFW_PRESS);

        float horizontal = 0.0f, vertical = 0.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) horizontal += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) horizontal -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) vertical += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) vertical -= 1.0f;
        Input::SetAxis("Horizontal", horizontal);
        Input::SetAxis("Vertical", vertical);

        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        glm::vec2 screenPoint;
        bool overAScreen = MapWindowPointToScreen(mouseX, mouseY, screenPoint);
        bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        Input::SetPointer(overAScreen && mouseDown, overAScreen ? screenPoint : glm::vec2{ 0.0f, 0.0f });

        AudioEngine::Update();

        // Before OnRuntimeUpdate, not after -- see DualityPlayer's own Main.cpp for why.
        UpdateUIInteractions(scene);

        double now = glfwGetTime();
        float deltaTime = static_cast<float>(now - lastTime);
        lastTime = now;

        scene.OnRuntimeUpdate(deltaTime);

        // A script-requested SceneManager.LoadScene is a deferred request (see
        // SceneManager.h's own comment) -- safe to act on now, right after
        // OnRuntimeUpdate returns and before this frame renders anything.
        if (SceneManager::HasPendingLoad()) {
            std::string pendingPath = SceneManager::ConsumePendingLoad();
            scene.OnRuntimeStop();
            // The old scene's textures/meshes are never referenced again once it's
            // replaced -- freed here rather than left cached for the rest of the
            // process's lifetime (see IRenderer2D::UnloadAllTextures's own comment).
            renderer.UnloadAllTextures();
            renderer3D.UnloadAllTextures();
            renderer3D.UnloadAllMeshes();
            scene = Scene();
            SceneSerializer(scene).Deserialize(project->GetAssetsDirectory() + "/" + pendingPath);
            scene.OnRuntimeStart();
        }

        renderer.BeginFrame();
        ScreenViewport top = TopViewport(), bottom = BottomViewport();
        RenderScreenIntoViewport(renderer, renderer3D, scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f }, top.X, top.Y, top.W, top.H);
        RenderScreenIntoViewport(renderer, renderer3D, scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f }, bottom.X, bottom.Y, bottom.W, bottom.H);
        renderer.EndFrame();

        glfwSwapBuffers(window);
    }

    scene.OnRuntimeStop();
    AudioEngine::Shutdown();
    renderer.Shutdown();
    renderer3D.Shutdown();
    glfwTerminate();
    return 0;
}
