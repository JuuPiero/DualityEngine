// DualityEditor -- Phase 1: Project system, JSON scene save/load, a minimal
// Content Browser, and a Play/Stop toggle that runs Behaviour lifecycle
// methods against the desktop preview.
//
// Still renders the same two-screen demo Scene through the desktop OpenGL
// backend, each screen into its own fixed-size framebuffer, displayed
// stacked in a "Device Preview" panel that mirrors the physical top/bottom
// layout of the console.

#include <cstdint>
#include <cstdio>
#include <type_traits>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h> // DockBuilder* -- for the initial Unity/Cocos-Creator-style dock layout

#include "DualityEditor/Framebuffer.h"
#include "DualityEditor/Panels/ContentBrowserPanel.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;

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

int main() {
    if (!glfwInit())
        return -1;

    GLFWwindow* window = glfwCreateWindow(1280, 800, "DualityEditor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glfwTerminate();
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool dockLayoutInitialized = false;

    RegisterBuiltinComponents();

    std::string buildDirectory = GetBuildDirectory();
    ScriptEngine::Reload(buildDirectory);

    OpenGLRenderer2D renderer;
    renderer.Init();

    Framebuffer topFramebuffer(TopScreenWidth, TopScreenHeight);
    Framebuffer bottomFramebuffer(BottomScreenWidth, BottomScreenHeight);

    // No Project Hub / "New Project" dialog yet -- always open (or create)
    // a fixed sample project next to the working directory.
    auto project = Project::New("SampleProject", "SampleProject");
    ContentBrowserPanel contentBrowser(project->GetAssetsDirectory());
    std::string scenePath = project->GetAssetsDirectory() + "/Scene.json";

    Scene scene;

    Entity topQuad = scene.CreateEntity("TopQuad");
    topQuad.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f, 0.0f };
    auto& topSprite = topQuad.AddComponent<SpriteRendererComponent>();
    topSprite.Size = { 80.0f, 80.0f };
    topSprite.Color = { 0.85f, 0.25f, 0.25f, 1.0f };
    topQuad.AddComponent<CameraComponent>().Screen = Screen::Top;

    Entity bottomQuad = scene.CreateEntity("BottomQuad");
    bottomQuad.GetComponent<TransformComponent>().Translation = { BottomScreenWidth * 0.5f, BottomScreenHeight * 0.5f, 0.0f };
    auto& bottomSprite = bottomQuad.AddComponent<SpriteRendererComponent>();
    bottomSprite.Size = { 60.0f, 60.0f };
    bottomSprite.Color = { 0.25f, 0.45f, 0.9f, 1.0f };
    bottomQuad.AddComponent<CameraComponent>().Screen = Screen::Bottom;
    bottomQuad.AddComponent<BehaviourComponent>().ClassName = "BounceBehaviour";

    Entity selected = topQuad;
    bool isPlaying = false;
    double lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        float deltaTime = static_cast<float>(now - lastFrameTime);
        lastFrameTime = now;

        if (isPlaying)
            scene.OnRuntimeUpdate(deltaTime);

        renderer.BeginFrame();
        topFramebuffer.Bind();
        RenderScreen(renderer, scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f });
        topFramebuffer.Unbind();
        bottomFramebuffer.Bind();
        RenderScreen(renderer, scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f });
        bottomFramebuffer.Unbind();
        renderer.EndFrame();

        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.15f, 0.15f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- Fullscreen dockspace host, Unity/Cocos-Creator-style default
        // layout (mirrors MyGameEngine's EditorLayer::OnUpdate) -----------
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("EditorDockHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("EditorDockspace");

        if (!dockLayoutInitialized) {
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
            ImGui::DockBuilderDockWindow("Device Preview", dockMain);
            ImGui::DockBuilderFinish(dockspaceId);

            dockLayoutInitialized = true;
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();

        ImGui::Begin("Device Preview");

        if (!isPlaying) {
            if (ImGui::Button("Play")) {
                isPlaying = true;
                scene.OnRuntimeStart();
            }
        } else {
            if (ImGui::Button("Stop")) {
                isPlaying = false;
                scene.OnRuntimeStop();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Scripts")) {
            ScriptEngine::Reload(buildDirectory);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Scene")) {
            SceneSerializer(scene).Serialize(scenePath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Scene")) {
            SceneSerializer(scene).Deserialize(scenePath);
        }

        ImGui::Separator();
        ImGui::Text("Top Screen (400x240)");
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(topFramebuffer.GetColorAttachment())),
                     ImVec2(static_cast<float>(TopScreenWidth), static_cast<float>(TopScreenHeight)), ImVec2(0, 1), ImVec2(1, 0));
        ImGui::Spacing();
        ImGui::Text("Bottom Screen (320x240, touch)");
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(bottomFramebuffer.GetColorAttachment())),
                     ImVec2(static_cast<float>(BottomScreenWidth), static_cast<float>(BottomScreenHeight)), ImVec2(0, 1), ImVec2(1, 0));
        ImGui::End();

        ImGui::Begin("Hierarchy");
        for (auto handle : scene.Registry().view<TagComponent>()) {
            Entity entity(handle, &scene);
            const bool isSelected = (entity == selected);
            if (ImGui::Selectable(entity.GetComponent<TagComponent>().Tag.c_str(), isSelected))
                selected = entity;
        }
        ImGui::End();

        ImGui::Begin("Properties");
        if (selected) {
            // Fully generic: adding a new component (or a new field to an
            // existing one) to TypeRegistry needs zero changes here.
            for (auto& type : TypeRegistry::All()) {
                if (!type.Has(selected))
                    continue;

                void* component = type.GetPtr(selected);
                if (ImGui::CollapsingHeader(type.DisplayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (auto& field : type.Fields) {
                        FieldValue value = field.Get(component);
                        bool changed = false;

                        std::visit([&](auto&& v) {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, glm::vec3>) {
                                changed = ImGui::DragFloat3(field.Name.c_str(), &v.x, 0.5f);
                            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                                changed = ImGui::DragFloat2(field.Name.c_str(), &v.x, 0.5f, 1.0f, 1000.0f);
                            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                                changed = ImGui::DragFloat4(field.Name.c_str(), &v.x, 0.01f);
                            } else if constexpr (std::is_same_v<T, Color4>) {
                                changed = ImGui::ColorEdit4(field.Name.c_str(), &v.Value.x);
                            } else if constexpr (std::is_same_v<T, float>) {
                                changed = ImGui::DragFloat(field.Name.c_str(), &v, 0.1f);
                            } else if constexpr (std::is_same_v<T, int>) {
                                changed = ImGui::DragInt(field.Name.c_str(), &v);
                            } else if constexpr (std::is_same_v<T, bool>) {
                                changed = ImGui::Checkbox(field.Name.c_str(), &v);
                            } else if constexpr (std::is_same_v<T, std::string>) {
                                char buffer[256];
                                std::snprintf(buffer, sizeof(buffer), "%s", v.c_str());
                                if (ImGui::InputText(field.Name.c_str(), buffer, sizeof(buffer))) {
                                    v = buffer;
                                    changed = true;
                                }
                            } else if constexpr (std::is_same_v<T, Screen>) {
                                const char* items[] = { "Top", "Bottom" };
                                int current = (v == Screen::Top) ? 0 : 1;
                                if (ImGui::Combo(field.Name.c_str(), &current, items, 2)) {
                                    v = (current == 0) ? Screen::Top : Screen::Bottom;
                                    changed = true;
                                }
                            }
                            if (changed)
                                field.Set(component, FieldValue(v));
                        }, value);
                    }
                }
            }
        }
        ImGui::End();

        contentBrowser.OnImGuiRender();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    if (isPlaying)
        scene.OnRuntimeStop();

    ScriptEngine::Shutdown();
    renderer.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
