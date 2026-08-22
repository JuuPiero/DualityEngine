// DualityEditor -- Project system, JSON scene save/load, a Content Browser,
// a Play/Stop toggle running Behaviour + Box2D lifecycle against the desktop
// preview, and Unity-style Scene/Game/Console panels:
//   - Scene:  free-roam editor-only camera over the whole scene (own
//             offscreen framebuffer, pan with right-drag, zoom with wheel,
//             left-click to select) -- no CameraComponent involved.
//   - Game:   exactly what the real TopCamera/BottomCamera entities render,
//             stacked top/bottom to mirror the console's physical layout --
//             the same RenderScreen pass DualityPlayer uses on-device.
//   - Console: Duality::Log's in-memory entries (see ConsolePanel.h).

#include <algorithm>
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

#include "DualityEditor/BuildPipeline.h"
#include "DualityEditor/Framebuffer.h"
#include "DualityEditor/Panels/ConsolePanel.h"
#include "DualityEditor/Panels/ContentBrowserPanel.h"
#include "DualityEditor/SceneGizmo.h"
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
    std::string repoRoot = buildDirectory.substr(0, buildDirectory.find_last_of("\\/")); // parent of "build-desktop"
    ScriptEngine::Reload(buildDirectory);

    OpenGLRenderer2D renderer;
    renderer.Init();

    Framebuffer topFramebuffer(TopScreenWidth, TopScreenHeight);
    Framebuffer bottomFramebuffer(BottomScreenWidth, BottomScreenHeight);

    // Scene view: a free-roam editor-only camera over a fixed-size canvas
    // (no CameraComponent involved), so the whole scene can be laid out in
    // one shared space regardless of which physical screen a given sprite
    // ends up on -- Unity's Scene view, as opposed to Game view below which
    // is exactly what the real TopCamera/BottomCamera entities see.
    constexpr int SceneViewWidth = 960;
    constexpr int SceneViewHeight = 540;
    Framebuffer sceneFramebuffer(SceneViewWidth, SceneViewHeight);
    glm::vec2 sceneCameraPos{ TopScreenWidth * 0.5f, TopScreenHeight * 0.5f };
    float sceneZoom = 1.0f;
    GizmoAxis draggingGizmoAxis = GizmoAxis::None;

    ConsolePanel consolePanel;

    // No Project Hub / "New Project" dialog yet -- always open (or create)
    // a fixed sample project next to the working directory.
    auto project = Project::New("SampleProject", "SampleProject");
    ContentBrowserPanel contentBrowser(project->GetAssetsDirectory());
    std::string scenePath = project->GetAssetsDirectory() + "/Scene.json";

    Scene scene;

    // Cameras are their own entities (no SpriteRendererComponent), matching
    // Unity/Cocos convention -- earlier revisions put CameraComponent
    // directly on TopQuad/BottomQuad, which conflated "what renders" with
    // "what's the viewpoint" and doesn't generalize once a screen needs to
    // show more than one sprite (which the renderer already supports).
    Entity topCamera = scene.CreateEntity("TopCamera");
    topCamera.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f, 0.0f };
    topCamera.AddComponent<CameraComponent>().Screen = Screen::Top;

    Entity bottomCamera = scene.CreateEntity("BottomCamera");
    bottomCamera.GetComponent<TransformComponent>().Translation = { BottomScreenWidth * 0.5f, BottomScreenHeight * 0.5f, 0.0f };
    bottomCamera.AddComponent<CameraComponent>().Screen = Screen::Bottom;

    Entity topQuad = scene.CreateEntity("TopQuad");
    topQuad.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f, 0.0f };
    auto& topSprite = topQuad.AddComponent<SpriteRendererComponent>();
    topSprite.Size = { 80.0f, 80.0f };
    topSprite.Color = { 0.85f, 0.25f, 0.25f, 1.0f };

    Entity bottomQuad = scene.CreateEntity("BottomQuad");
    bottomQuad.GetComponent<TransformComponent>().Translation = { BottomScreenWidth * 0.5f, BottomScreenHeight * 0.5f, 0.0f };
    auto& bottomSprite = bottomQuad.AddComponent<SpriteRendererComponent>();
    bottomSprite.Size = { 60.0f, 60.0f };
    bottomSprite.Color = { 0.25f, 0.45f, 0.9f, 1.0f };
    bottomQuad.AddComponent<BehaviourComponent>().ClassName = "BounceBehaviour";

    // Physics demo: a ball falls onto a static platform when Play starts,
    // proving Box2D integration + the camera-relative multi-sprite
    // rendering (three sprites now share TopCamera, not just TopQuad's own).
    Entity physicsGround = scene.CreateEntity("PhysicsGround");
    physicsGround.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, 220.0f, 0.0f };
    auto& groundSprite = physicsGround.AddComponent<SpriteRendererComponent>();
    groundSprite.Size = { 360.0f, 16.0f };
    groundSprite.Color = { 0.3f, 0.75f, 0.35f, 1.0f };
    auto& groundBody = physicsGround.AddComponent<Rigidbody2DComponent>();
    groundBody.IsStatic = true;
    auto& groundCollider = physicsGround.AddComponent<BoxCollider2DComponent>();
    groundCollider.Size = { 180.0f, 8.0f };

    Entity physicsBall = scene.CreateEntity("PhysicsBall");
    physicsBall.GetComponent<TransformComponent>().Translation = { 100.0f, 40.0f, 0.0f };
    auto& ballSprite = physicsBall.AddComponent<SpriteRendererComponent>();
    ballSprite.Size = { 20.0f, 20.0f };
    ballSprite.Color = { 0.95f, 0.85f, 0.2f, 1.0f };
    physicsBall.AddComponent<Rigidbody2DComponent>();
    auto& ballCollider = physicsBall.AddComponent<CircleCollider2DComponent>();
    ballCollider.Radius = 10.0f;
    ballCollider.Restitution = 0.4f;

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

        // BeginCustomView's projection is already centered on
        // sceneCameraPos/sceneZoom (see OpenGLRenderer2D::BeginCustomView),
        // unlike BeginScene's fixed 0..width/0..height projection that
        // RenderScreen manually pre-transforms into before drawing -- so
        // DrawQuad calls here must use plain world coordinates. Manually
        // re-applying the camera offset/zoom on top of that (as an earlier
        // version of this loop did) double-transforms every position,
        // drifting further from the gizmo/selection math -- which already
        // computes a single correct transform -- the more the camera pans
        // or zooms away from its default.
        sceneFramebuffer.Bind();
        renderer.BeginCustomView(sceneCameraPos, sceneZoom, static_cast<float>(SceneViewWidth), static_cast<float>(SceneViewHeight), { 0.15f, 0.15f, 0.18f, 1.0f });
        for (auto handle : scene.Registry().view<TransformComponent, SpriteRendererComponent>()) {
            auto& transform = scene.Registry().get<TransformComponent>(handle);
            auto& sprite = scene.Registry().get<SpriteRendererComponent>(handle);
            glm::vec2 topLeft{ transform.Translation.x - sprite.Size.x * 0.5f, transform.Translation.y - sprite.Size.y * 0.5f };
            renderer.DrawQuad(topLeft, sprite.Size, sprite.Color);
        }
        // Cameras have no sprite of their own -- draw a small gizmo marker at
        // each one's position (color-coded by target screen) so the Scene
        // view still shows where TopCamera/BottomCamera actually are. Sized
        // in world units scaled inversely by zoom so it reads as a constant
        // screen size (like a real editor camera-frustum icon) rather than
        // shrinking/growing with the world content around it.
        for (auto handle : scene.Registry().view<TransformComponent, CameraComponent>()) {
            auto& transform = scene.Registry().get<TransformComponent>(handle);
            auto& camera = scene.Registry().get<CameraComponent>(handle);
            float markerSize = 14.0f / sceneZoom;
            glm::vec4 markerColor = (camera.Screen == Screen::Top) ? glm::vec4{ 0.3f, 0.9f, 0.9f, 1.0f } : glm::vec4{ 0.95f, 0.6f, 0.2f, 1.0f };
            renderer.DrawQuad({ transform.Translation.x - markerSize * 0.5f, transform.Translation.y - markerSize * 0.5f }, { markerSize, markerSize }, markerColor);
        }
        renderer.EndScene();
        sceneFramebuffer.Unbind();

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
            ImGui::DockBuilderDockWindow("Console", dockBottom);
            // Scene and Game share the same center dock node, so they come up
            // as tabs -- matching Unity's default layout exactly.
            ImGui::DockBuilderDockWindow("Scene", dockMain);
            ImGui::DockBuilderDockWindow("Game", dockMain);
            ImGui::DockBuilderFinish(dockspaceId);

            dockLayoutInitialized = true;
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();

        ImGui::Begin("Scene");
        {
            ImVec2 imagePos = ImGui::GetCursorScreenPos();
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(sceneFramebuffer.GetColorAttachment())),
                         ImVec2(static_cast<float>(SceneViewWidth), static_cast<float>(SceneViewHeight)), ImVec2(0, 1), ImVec2(1, 0));
            bool imageHovered = ImGui::IsItemHovered();
            ImGuiIO& io = ImGui::GetIO();

            auto WorldToSceneScreen = [&](const glm::vec2& worldPos) {
                return ImVec2(
                    imagePos.x + (worldPos.x - sceneCameraPos.x) * sceneZoom + SceneViewWidth * 0.5f,
                    imagePos.y + (worldPos.y - sceneCameraPos.y) * sceneZoom + SceneViewHeight * 0.5f);
            };

            // Draw+hit-test the gizmo every frame (not just while hovered) so
            // a drag already in progress keeps tracking even if the mouse
            // drifts outside the image mid-drag.
            bool hasGizmoTarget = selected && selected.HasComponent<TransformComponent>();
            GizmoAxis hoveredGizmoAxis = GizmoAxis::None;
            if (hasGizmoTarget) {
                auto& selectedTransform = selected.GetComponent<TransformComponent>();
                ImVec2 gizmoOrigin = WorldToSceneScreen({ selectedTransform.Translation.x, selectedTransform.Translation.y });
                hoveredGizmoAxis = DrawAndHitTestGizmo2D(gizmoOrigin, draggingGizmoAxis);
            }

            if (imageHovered) {
                if (io.MouseWheel != 0.0f)
                    sceneZoom = std::clamp(sceneZoom * (1.0f + io.MouseWheel * 0.1f), 0.1f, 5.0f);

                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                    sceneCameraPos.x -= io.MouseDelta.x / sceneZoom;
                    sceneCameraPos.y -= io.MouseDelta.y / sceneZoom;
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (hasGizmoTarget && hoveredGizmoAxis != GizmoAxis::None) {
                        draggingGizmoAxis = hoveredGizmoAxis;
                    } else {
                        glm::vec2 local{ io.MousePos.x - imagePos.x, io.MousePos.y - imagePos.y };
                        glm::vec2 worldPoint{
                            (local.x - SceneViewWidth * 0.5f) / sceneZoom + sceneCameraPos.x,
                            (local.y - SceneViewHeight * 0.5f) / sceneZoom + sceneCameraPos.y
                        };

                        Entity hit;
                        for (auto handle : scene.Registry().view<TransformComponent, SpriteRendererComponent>()) {
                            Entity candidate(handle, &scene);
                            auto& transform = candidate.GetComponent<TransformComponent>();
                            auto& sprite = candidate.GetComponent<SpriteRendererComponent>();
                            bool inside = worldPoint.x >= transform.Translation.x - sprite.Size.x * 0.5f &&
                                          worldPoint.x <= transform.Translation.x + sprite.Size.x * 0.5f &&
                                          worldPoint.y >= transform.Translation.y - sprite.Size.y * 0.5f &&
                                          worldPoint.y <= transform.Translation.y + sprite.Size.y * 0.5f;
                            if (inside)
                                hit = candidate; // topmost (last drawn) match wins
                        }
                        if (hit)
                            selected = hit;
                    }
                }
            }

            // Not gated on imageHovered: once a drag starts it should keep
            // following the mouse even if the cursor leaves the image rect,
            // matching how ImGui's own drag widgets behave.
            if (draggingGizmoAxis != GizmoAxis::None) {
                if (hasGizmoTarget && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    auto& selectedTransform = selected.GetComponent<TransformComponent>();
                    glm::vec2 worldDelta{ io.MouseDelta.x / sceneZoom, io.MouseDelta.y / sceneZoom };
                    if (draggingGizmoAxis == GizmoAxis::X || draggingGizmoAxis == GizmoAxis::Both)
                        selectedTransform.Translation.x += worldDelta.x;
                    if (draggingGizmoAxis == GizmoAxis::Y || draggingGizmoAxis == GizmoAxis::Both)
                        selectedTransform.Translation.y += worldDelta.y;
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    draggingGizmoAxis = GizmoAxis::None;
            }
        }
        ImGui::End();

        ImGui::Begin("Game");

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
        ImGui::SameLine();
        if (ImGui::Button("Build for 3DS")) {
            SceneSerializer(scene).Serialize(scenePath); // build packages the last-saved scene
            BuildPipeline::BuildFor3DS(repoRoot, scenePath);
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
        for (auto handle : scene.Registry().view<NameComponent>()) {
            Entity entity(handle, &scene);
            const bool isSelected = (entity == selected);
            if (ImGui::Selectable(entity.GetComponent<NameComponent>().Name.c_str(), isSelected))
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
        consolePanel.OnImGuiRender();

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
