#include "DualityEditor/Application.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* -- for the initial Unity/Cocos-Creator-style dock layout

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FileDialogs.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    // Derives the CMake build directory (e.g. ".../build-desktop") from this
    // executable's own path, so ScriptEngine can find/rebuild GameScripts.dll
    // regardless of the current working directory the Editor was launched from.
    static std::string GetBuildDirectory() {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string exePath = path;
        size_t pos = exePath.find_last_of("\\/");
        exePath = exePath.substr(0, pos); // strip "DualityEditor.exe"
        pos = exePath.find_last_of("\\/");
        exePath = exePath.substr(0, pos); // strip "DualityEditor"
        return exePath;
    }

    Application::Application()
        : m_Window(1280, 800, "DualityEditor")
        , m_Project(Project::New("SampleProject", "SampleProject"))
        , m_TopFramebuffer(TopScreenWidth, TopScreenHeight)
        , m_BottomFramebuffer(BottomScreenWidth, BottomScreenHeight)
        , m_SceneFramebuffer(960, 540) // resized every frame to match the Scene panel -- see ScenePanel
        , m_ContentBrowserPanel(m_Project->GetAssetsDirectory()) {
        m_Window.SetEventCallback([this](Event& e) { OnEvent(e); });
        // Dropped files (e.g. from Windows Explorer) always import into
        // whatever directory the Content Browser is currently showing --
        // no position-hit-testing against which panel they landed on,
        // matching the simpler behavior confirmed in MyGameEngine's own
        // EditorLayer::OnFilesDropped.
        m_Window.SetDropCallback([this](const std::vector<std::string>& files) {
            for (const std::string& file : files)
                m_ContentBrowserPanel.ImportFile(file);
        });

        RegisterBuiltinComponents();

        m_BuildDirectory = GetBuildDirectory();
        m_RepoRoot = m_BuildDirectory.substr(0, m_BuildDirectory.find_last_of("\\/")); // parent of "build-desktop"
        ScriptEngine::Reload(m_BuildDirectory);

        m_Renderer.Init();
        AudioEngine::Init();

        m_ScenePath = m_Project->GetAssetsDirectory() + "/Scene.json";
        m_SceneCameraPos = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f };
        AssetDatabase::Refresh(m_Project->GetAssetsDirectory());

        SetupDemoScene();
    }

    void Application::SetupDemoScene() {
        // Cameras are their own entities (no SpriteRendererComponent),
        // matching Unity/Cocos convention.
        Entity topCamera = m_Scene.CreateEntity("TopCamera");
        topCamera.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f, 0.0f };
        topCamera.AddComponent<CameraComponent>().Screen = Screen::Top;

        Entity bottomCamera = m_Scene.CreateEntity("BottomCamera");
        bottomCamera.GetComponent<TransformComponent>().Translation = { BottomScreenWidth * 0.5f, BottomScreenHeight * 0.5f, 0.0f };
        bottomCamera.AddComponent<CameraComponent>().Screen = Screen::Bottom;

        Entity topQuad = m_Scene.CreateEntity("TopQuad");
        topQuad.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f, 0.0f };
        auto& topSprite = topQuad.AddComponent<SpriteRendererComponent>();
        topSprite.Size = { 80.0f, 80.0f };
        topSprite.Color = { 0.85f, 0.25f, 0.25f, 1.0f };

        Entity bottomQuad = m_Scene.CreateEntity("BottomQuad");
        bottomQuad.GetComponent<TransformComponent>().Translation = { BottomScreenWidth * 0.5f, BottomScreenHeight * 0.5f, 0.0f };
        auto& bottomSprite = bottomQuad.AddComponent<SpriteRendererComponent>();
        bottomSprite.Size = { 60.0f, 60.0f };
        bottomSprite.Color = { 0.25f, 0.45f, 0.9f, 1.0f };
        bottomQuad.AddComponent<BehaviourComponent>().ClassName = "BounceBehaviour";

        // Physics demo: a ball falls onto a static platform when Play
        // starts, proving Box2D integration + camera-relative multi-sprite
        // rendering.
        Entity physicsGround = m_Scene.CreateEntity("PhysicsGround");
        physicsGround.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, 220.0f, 0.0f };
        auto& groundSprite = physicsGround.AddComponent<SpriteRendererComponent>();
        groundSprite.Size = { 360.0f, 16.0f };
        groundSprite.Color = { 0.3f, 0.75f, 0.35f, 1.0f };
        auto& groundBody = physicsGround.AddComponent<Rigidbody2DComponent>();
        groundBody.IsStatic = true;
        auto& groundCollider = physicsGround.AddComponent<BoxCollider2DComponent>();
        groundCollider.Size = { 180.0f, 8.0f };

        Entity physicsBall = m_Scene.CreateEntity("PhysicsBall");
        physicsBall.GetComponent<TransformComponent>().Translation = { 100.0f, 40.0f, 0.0f };
        auto& ballSprite = physicsBall.AddComponent<SpriteRendererComponent>();
        ballSprite.Size = { 20.0f, 20.0f };
        ballSprite.Color = { 0.95f, 0.85f, 0.2f, 1.0f };
        physicsBall.AddComponent<Rigidbody2DComponent>();
        auto& ballCollider = physicsBall.AddComponent<CircleCollider2DComponent>();
        ballCollider.Radius = 10.0f;
        ballCollider.Restitution = 0.4f;

        // Living documentation for the scripting API surface -- Input,
        // SaveSystem, DateTime, AudioEngine -- all exercised by
        // ApiShowcaseBehaviour (see GameScripts/Source/
        // ApiShowcaseBehaviour.cpp). Move it with WASD/arrows (or the
        // Circle Pad on 3DS), hold the mouse/touch to pull it toward the
        // pointer, press Space/A for a beep, watch it spin once a minute
        // driven by the real clock.
        Entity apiShowcase = m_Scene.CreateEntity("ApiShowcase");
        apiShowcase.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, 60.0f, 0.0f };
        auto& showcaseSprite = apiShowcase.AddComponent<SpriteRendererComponent>();
        showcaseSprite.Size = { 32.0f, 32.0f };
        apiShowcase.AddComponent<BehaviourComponent>().ClassName = "ApiShowcaseBehaviour";

        m_Selected = topQuad;
    }

    void Application::OpenProjectFromDialog() {
        std::string path = FileDialogs::OpenFile(m_Window.GetNativeWindow(), "Duality Project (*.dproj)\0*.dproj\0");
        if (path.empty())
            return;

        auto project = Project::Load(path);
        if (!project)
            return;

        if (m_IsPlaying) {
            m_Scene.OnRuntimeStop();
            m_IsPlaying = false;
        }

        m_Project = project;
        m_Scene = Scene(); // old Entity handles (including m_Selected) don't survive this
        m_Selected = Entity();
        m_ScenePath = m_Project->GetAssetsDirectory() + "/Scene.json";
        m_ContentBrowserPanel.SetRootDirectory(m_Project->GetAssetsDirectory());
        AssetDatabase::Refresh(m_Project->GetAssetsDirectory());

        // A brand new project has no Scene.json yet -- Deserialize logs an
        // error and leaves m_Scene empty in that case, same as clicking
        // "Load Scene" against a project that hasn't saved one yet.
        SceneSerializer(m_Scene).Deserialize(m_ScenePath);
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent&) {
            m_Running = false;
            return true;
        });
        // WindowResizeEvent has nothing to react to yet -- Scene/Game panels
        // size their own framebuffers off ImGui::GetContentRegionAvail(),
        // not off the OS window size -- but routing it through here (rather
        // than not having it at all) is what lets a future feature react to
        // an OS-level resize without Application needing to know GLFW exists.
    }

    void Application::Run() {
        double lastFrameTime = m_Window.GetTime();

        while (m_Running && !m_Window.ShouldClose()) {
            double now = m_Window.GetTime();
            float deltaTime = static_cast<float>(now - lastFrameTime);
            lastFrameTime = now;

            AudioEngine::Update();

            if (m_IsPlaying)
                m_Scene.OnRuntimeUpdate(deltaTime);

            m_Renderer.BeginFrame();
            m_TopFramebuffer.Bind();
            RenderScreen(m_Renderer, m_Scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f });
            m_TopFramebuffer.Unbind();
            m_BottomFramebuffer.Bind();
            RenderScreen(m_Renderer, m_Scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f });
            m_BottomFramebuffer.Unbind();
            m_Renderer.EndFrame();

            m_Window.BeginFrame();

            // --- Fullscreen dockspace host + menu bar, Unity/Cocos-
            // Creator-style default layout (mirrors MyGameEngine's
            // EditorLayer::OnUpdate) -------------------------------------
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);

            ImGuiWindowFlags hostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                          ImGuiWindowFlags_NoNavFocus;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("EditorDockHost", nullptr, hostFlags);
            ImGui::PopStyleVar(3);

            ImGuiID dockspaceId = ImGui::GetID("EditorDockspace");

            if (!m_DockLayoutInitialized) {
                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

                ImGuiID dockMain = dockspaceId;
                ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.2f, nullptr, &dockMain);
                ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.28f, nullptr, &dockMain);
                ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

                ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
                ImGui::DockBuilderDockWindow("Properties", dockRight);
                ImGui::DockBuilderDockWindow("Content Browser", dockBottom);
                ImGui::DockBuilderDockWindow("Console", dockBottom);
                // Scene and Game share the same center dock node, so they
                // come up as tabs -- matching Unity's default layout.
                ImGui::DockBuilderDockWindow("Scene", dockMain);
                ImGui::DockBuilderDockWindow("Game", dockMain);
                ImGui::DockBuilderFinish(dockspaceId);

                m_DockLayoutInitialized = true;
            }

            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

            EditorContext ctx{
                m_Scene, m_Selected, m_IsPlaying,
                m_SceneCameraPos, m_SceneZoom, m_ActiveGizmoMode, m_DraggingGizmoAxis,
                m_SceneFramebuffer, m_TopFramebuffer, m_BottomFramebuffer, m_Renderer,
                m_ScenePath, m_BuildDirectory, m_RepoRoot,
                m_RequestOpenProject
            };

            m_MenuBarPanel.OnImGuiRender(ctx);
            ImGui::End(); // EditorDockHost

            // Handled here (not inside MenuBarPanel) since opening a project
            // means swapping Scene/Project/ContentBrowser state that only
            // Application owns -- done before the rest of this frame's
            // panels render so they immediately reflect the new project
            // instead of showing one stale frame first.
            if (m_RequestOpenProject) {
                m_RequestOpenProject = false;
                OpenProjectFromDialog();
            }

            m_ScenePanel.OnImGuiRender(ctx);
            m_GamePanel.OnImGuiRender(ctx);
            m_HierarchyPanel.OnImGuiRender(ctx);
            m_PropertiesPanel.OnImGuiRender(ctx);
            m_ContentBrowserPanel.OnImGuiRender();
            m_ConsolePanel.OnImGuiRender();

            m_Window.EndFrame();
        }

        if (m_IsPlaying)
            m_Scene.OnRuntimeStop();

        ScriptEngine::Shutdown();
        m_Renderer.Shutdown();
        AudioEngine::Shutdown();
    }

}
