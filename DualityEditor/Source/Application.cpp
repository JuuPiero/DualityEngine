#include "DualityEditor/Application.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* -- for the initial Unity/Cocos-Creator-style dock layout

#include "DualityEditor/AssetInspectors.h"
#include "DualityEditor/Import/ModelImportUI.h"
#include "DualityEditor/BuiltinMeshPreviews.h"
#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorSettings.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/FileDialogs.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEditor/TypeScriptCompiler.h"
#include "DualityEngine/Scripting/JS/JsScripting.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/PhysicsMaterialLoader.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Core/Profiler.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Renderer/RenderSettings.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/PhysicsRaycaster.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    void Application::StopAdditiveRuntimeScenes() {
        for (auto& loaded : m_AdditiveRuntimeScenes)
            loaded.Value->OnRuntimeStop();
        m_AdditiveRuntimeScenes.clear();
        // A Stop/Reload operation cancels work issued by the just-stopped scripts. Without
        // this, a stale deferred request could reload a scene when the next Play begins.
        SceneManager::ClearPendingRequests();
    }

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

    static std::shared_ptr<Project> LoadDefaultProject(const std::string& requestedPath) {
        if (!requestedPath.empty()) {
            if (auto project = Project::Load(requestedPath))
                return project;
            Log::Error("Could not open project '" + requestedPath + "', falling back to SampleProject");
        }
        constexpr const char* DefaultProjectPath = "SampleProject/SampleProject.dproj";
        if (std::filesystem::exists(DefaultProjectPath)) {
            if (auto project = Project::Load(DefaultProjectPath))
                return project;
        }
        return Project::New("SampleProject", "SampleProject");
    }

    static std::string GetStartupScenePath(const std::shared_ptr<Project>& project) {
        return project->GetAssetsDirectory() + "/" + project->GetStartScenePath();
    }

    Application::Application(const ApplicationOptions& options)
        : m_Window(options.Width, options.Height, "DualityEditor", !options.TestScript.empty())
        , m_Project(LoadDefaultProject(options.ProjectPath))
        , m_TopFramebuffer(TopScreenWidth, TopScreenHeight)
        , m_BottomFramebuffer(BottomScreenWidth, BottomScreenHeight)
        , m_TopSceneFramebuffer(480, 540) // resized every frame to match its Scene view pane -- see ScenePanel
        , m_BottomSceneFramebuffer(480, 540)
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
        RegisterBuiltinAssetInspectors();

        m_BuildDirectory = GetBuildDirectory();
        m_RepoRoot = m_BuildDirectory.substr(0, m_BuildDirectory.find_last_of("\\/")); // parent of "build-desktop"
        ScriptEngine::Reload(m_BuildDirectory);
        // TypeScript scripts (QuickJS) -- compiled + loaded before the start scene is read so
        // every TS class is registered when its Scripts entries deserialize.
        TypeScriptCompiler::CompileAndLoadSync(m_RepoRoot, m_Project->GetDirectory());

        m_Renderer.Init();
        m_Renderer3D.Init();
        RenderSettings::SetShadowMode(m_Project->GetConfig().ShadowTechnique);
        AudioEngine::Init();

        m_ScenePath = GetStartupScenePath(m_Project);
        // m_TopSceneView/m_BottomSceneView start unseeded -- ScenePanel seeds each
        // from that screen's real primary CameraComponent on its first render.
        AssetDatabase::Refresh(m_Project->GetAssetsDirectory());

        if (std::filesystem::exists(m_ScenePath))
            SceneSerializer(m_Scene).Deserialize(m_ScenePath);
        else
            SetupDemoScene();
        m_SceneHistory.Reset(m_Scene, m_Session.Ids);

        m_Options = options;
        m_Session.RunAfterSavePrompt = [this](std::function<void(EditorContext&)> action) { RunAfterSavePrompt(std::move(action)); };

        // Reference fields (Inspector object fields): click pings, double-click selects.
        FieldEditorHooks hooks;
        hooks.PingAsset = [this](const std::string& path) {
            m_Session.PingAssetPath = path;
            m_Session.PingStartTime = ImGui::GetTime();
        };
        hooks.SelectAsset = [this](const std::string& path) {
            m_Selected = Entity{};
            m_SelectedEntities.clear();
            m_SelectedAssetPath = path;
            m_SelectedAssetPaths = { path };
            m_Session.PingAssetPath = path;
            m_Session.PingStartTime = ImGui::GetTime();
        };
        hooks.PingEntity = [this](uint32_t handle) {
            Entity entity(static_cast<entt::entity>(handle), &m_Scene);
            if (!entity)
                return;
            m_Session.PingEntityId = m_Session.Ids.Get(entity);
            m_Session.PingStartTime = ImGui::GetTime();
        };
        hooks.SelectEntity = [this](uint32_t handle) {
            Entity entity(static_cast<entt::entity>(handle), &m_Scene);
            if (!entity)
                return;
            m_Selected = entity;
            m_SelectedEntities = { entity };
            m_SelectedAssetPath.clear();
            m_SelectedAssetPaths.clear();
            m_Session.RequestRevealSelection = true;
        };
        hooks.BuiltinMeshThumbnail = [this](MeshPrimitive primitive) {
            return BuiltinMeshPreviews::Get(m_Renderer3D, primitive);
        };
        SetFieldEditorHooks(std::move(hooks));
        if (!options.TestScript.empty())
            m_Automation.Load(options.TestScript);
        else
            RecordRecentProject();
    }

    void Application::RecordRecentProject() {
        if (!m_Project)
            return;
        const std::string path = m_Project->GetDirectory() + "/" + m_Project->GetConfig().Name + ".dproj";
        if (!std::filesystem::exists(path))
            return;
        EditorSettings::Get().AddRecentProject(path);
        EditorSettings::Get().Save();
    }

    void Application::RunAfterSavePrompt(std::function<void(EditorContext&)> action) {
        // Unity asks only about real unsaved scene edits; Play-mode changes are transient.
        m_PendingSavePromptAction = std::move(action);
        m_OpenSavePrompt = true;
    }

    void Application::EnterPrefabMode(const std::string& prefabPath) {
        if (m_IsPlaying || prefabPath.empty())
            return;
        if (m_IsEditingPrefab)
            ExitPrefabMode();

        m_PrefabModeSceneSnapshot = SceneSerializer(m_Scene).SerializeToJson().dump();
        m_PrefabModeIds = m_Session.Ids.CaptureOrder(m_Scene);
        m_PrefabModeOriginalScenePath = m_ScenePath;
        m_PrefabModeOriginalDirty = m_SceneDirty;
        m_Scene.Clear();
        m_Selected = Entity{};
        m_SelectedEntities.clear();
        m_SelectedAssetPath.clear();
        m_SelectedAssetPaths.clear();
        m_TopSceneView = SceneViewCamera();
        m_BottomSceneView = SceneViewCamera();
        m_TopSceneView3D = SceneViewCamera3D();
        m_BottomSceneView3D = SceneViewCamera3D();

        const std::string guid = AssetMeta::EnsureMetaFile(prefabPath);
        AssetDatabase::Register(guid, prefabPath);
        Entity root = PrefabSerializer::Instantiate(m_Scene, prefabPath, {}, AssetRef{ guid });
        if (!root) {
            // Do not strand the user in an empty editor if a corrupt prefab was opened.
            SceneSerializer(m_Scene).DeserializeFromJson(nlohmann::json::parse(m_PrefabModeSceneSnapshot));
            m_ScenePath = m_PrefabModeOriginalScenePath;
            m_SceneDirty = m_PrefabModeOriginalDirty;
            return;
        }

        m_IsEditingPrefab = true;
        m_EditingPrefabPath = prefabPath;
        m_Selected = root;
        m_SelectedEntities = { root };
        m_SceneDirty = false;
        m_SceneHistory.Reset(m_Scene, m_Session.Ids);
        m_CapturedSceneChangeSerial = m_SceneChangeSerial;
        Log::Info("Entered Prefab Mode: '" + prefabPath + "'");
    }

    void Application::ExitPrefabMode() {
        if (!m_IsEditingPrefab)
            return;

        if (m_SceneDirty && !m_Scene.GetRootEntities().empty())
            PrefabSerializer::Save(m_Scene.GetRootEntities().front(), m_EditingPrefabPath);

        m_Scene.Clear();
        try {
            SceneSerializer(m_Scene).DeserializeFromJson(nlohmann::json::parse(m_PrefabModeSceneSnapshot));
            m_Session.Ids.ApplyOrder(m_Scene, m_PrefabModeIds);
        } catch (const nlohmann::json::exception& e) {
            Log::Error(std::string("Could not restore scene after Prefab Mode: ") + e.what());
        }
        m_ScenePath = m_PrefabModeOriginalScenePath;
        m_SceneDirty = m_PrefabModeOriginalDirty;
        m_IsEditingPrefab = false;
        m_EditingPrefabPath.clear();
        m_Selected = Entity{};
        m_SelectedEntities.clear();
        m_SelectedAssetPath.clear();
        m_SelectedAssetPaths.clear();
        m_SceneHistory.Reset(m_Scene, m_Session.Ids);
        m_CapturedSceneChangeSerial = m_SceneChangeSerial;
        Log::Info("Exited Prefab Mode");
    }

    // One clean showcase scene demonstrating every engine technology/component, replacing what
    // had become 16 entities accumulated ad hoc over many sessions (two of them still labeled
    // "TEMPORARY, to be reverted" long after becoming permanent). Grouped under plain marker
    // entities (a Name + the default identity Transform, no renderable component of their own)
    // via Scene::SetParent so the Hierarchy panel reads as a categorized tree instead of one
    // long flat list -- purely organizational: none of these markers carry a UIRectComponent or
    // UILayoutGroupComponent, so parenting under them never changes a child's resolved world
    // position (Transform-based) or resolved rect (UIRectComponent-based) at all.
    void Application::SetupDemoScene() {
        auto group = [&](const char* name) { return m_Scene.CreateEntity(name); };

        // ---------------------------------------------------------------- Cameras
        Entity camerasGroup = group("-- Cameras --");

        Entity topCamera = m_Scene.CreateEntity("TopCamera");
        topCamera.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f, 0.0f };
        topCamera.AddComponent<CameraComponent>().Screen = Screen::Top;
        topCamera.AddComponent<PhysicsRaycaster2DComponent>();
        m_Scene.SetParent(topCamera, camerasGroup);

        // Bottom screen runs Perspective/3D -- every 3D demo below (mesh rendering, Bullet
        // physics, 3D raycasting) renders and is clickable through this camera.
        Entity bottomCamera = m_Scene.CreateEntity("BottomCamera");
        auto& bottomCameraComponent = bottomCamera.AddComponent<CameraComponent>();
        bottomCamera.AddComponent<PhysicsRaycaster3DComponent>();
        bottomCameraComponent.Screen = Screen::Bottom;
        bottomCameraComponent.Projection = ProjectionType::Perspective;
        bottomCamera.GetComponent<TransformComponent>().Translation = { 0.0f, 60.0f, 200.0f };
        bottomCamera.GetComponent<TransformComponent>().Rotation = { -10.0f, 0.0f, 0.0f };
        m_Scene.SetParent(bottomCamera, camerasGroup);

        // ---------------------------------------------------------------- Rendering
        Entity renderingGroup = group("-- Rendering --");

        // Real asset pipeline exercise (file + .meta + AssetDatabase), not just a
        // MeshRendererComponent field -- reused by both 3D mesh entities below via
        // MeshRendererComponent::Materials (a real list now, not a single AssetRef).
        std::filesystem::path materialsDir = m_Project->GetAssetsDirectory() + "/Materials";
        std::filesystem::create_directories(materialsDir);
        std::filesystem::path materialPath = materialsDir / "TestOrange.mat";
        MaterialLoader::Save(materialPath.string(), Material{ { 0.9f, 0.5f, 0.2f, 1.0f }, AssetRef{} });
        std::string materialGuid = AssetMeta::EnsureMetaFile(materialPath);
        AssetDatabase::Register(materialGuid, materialPath.string());

        Entity testCube = m_Scene.CreateEntity("TestCube3D");
        testCube.GetComponent<TransformComponent>().Rotation = { 20.0f, 35.0f, 0.0f };
        testCube.GetComponent<TransformComponent>().Scale = { 60.0f, 60.0f, 60.0f };
        testCube.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Cube);
        auto& testCubeMesh = testCube.AddComponent<MeshRendererComponent>();
        testCubeMesh.Materials.push_back(AssetRef{ materialGuid });
        // Same script class as the ApiShowcase entity below -- demonstrates it adapting to a 3D
        // mesh entity instead of a sprite (ground-plane movement, yaw spin) via
        // GetEntity().HasComponent<T>(), see ApiShowcaseBehaviour.cpp.
        testCube.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "ApiShowcaseBehaviour" });
        m_Scene.SetParent(testCube, renderingGroup);

        Entity topQuad = m_Scene.CreateEntity("TopQuad");
        topQuad.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, TopScreenHeight * 0.5f, 0.0f };
        auto& topSprite = topQuad.AddComponent<SpriteRendererComponent>();
        topSprite.Size = { 80.0f, 80.0f };
        topSprite.Color = { 0.85f, 0.25f, 0.25f, 1.0f };
        m_Scene.SetParent(topQuad, renderingGroup);

        Entity bottomQuad = m_Scene.CreateEntity("BottomQuad");
        bottomQuad.GetComponent<TransformComponent>().Translation = { BottomScreenWidth * 0.5f, BottomScreenHeight * 0.5f, 0.0f };
        auto& bottomSprite = bottomQuad.AddComponent<SpriteRendererComponent>();
        bottomSprite.Size = { 60.0f, 60.0f };
        bottomSprite.Color = { 0.25f, 0.45f, 0.9f, 1.0f };
        bottomQuad.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "BounceBehaviour" });
        m_Scene.SetParent(bottomQuad, renderingGroup);

        // ---------------------------------------------------------------- Physics 2D
        Entity physics2DGroup = group("-- Physics 2D --");

        // Reusable friction/restitution asset (real .physmat + .meta + AssetDatabase pipeline,
        // same 3-step registration as the Material above) -- assigned to PhysicsCapsule2D below
        // to prove a collider's own Friction/Restitution get overridden by it.
        std::filesystem::path physMatDir = m_Project->GetAssetsDirectory() + "/Physics";
        std::filesystem::create_directories(physMatDir);
        std::filesystem::path physMatPath = physMatDir / "Bouncy.physmat";
        PhysicsMaterialLoader::Save(physMatPath.string(), PhysicsMaterial{ 0.2f, 0.9f, 1.0f });
        std::string bouncyMaterialGuid = AssetMeta::EnsureMetaFile(physMatPath);
        AssetDatabase::Register(bouncyMaterialGuid, physMatPath.string());

        // A ball falls onto a static platform when Play starts, proving Box2D integration,
        // camera-relative multi-sprite rendering, and collision callback logging.
        Entity physicsGround = m_Scene.CreateEntity("PhysicsGround");
        physicsGround.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, 220.0f, 0.0f };
        auto& groundSprite = physicsGround.AddComponent<SpriteRendererComponent>();
        groundSprite.Size = { 360.0f, 16.0f };
        groundSprite.Color = { 0.3f, 0.75f, 0.35f, 1.0f };
        physicsGround.AddComponent<Rigidbody2DComponent>().Type = BodyType::Static;
        physicsGround.AddComponent<BoxCollider2DComponent>().Size = { 180.0f, 8.0f };
        m_Scene.SetParent(physicsGround, physics2DGroup);

        Entity physicsBall = m_Scene.CreateEntity("PhysicsBall");
        physicsBall.GetComponent<TransformComponent>().Translation = { 100.0f, 40.0f, 0.0f };
        auto& ballSprite = physicsBall.AddComponent<SpriteRendererComponent>();
        ballSprite.Size = { 20.0f, 20.0f };
        ballSprite.Color = { 0.95f, 0.85f, 0.2f, 1.0f };
        physicsBall.AddComponent<Rigidbody2DComponent>();
        auto& ballCollider = physicsBall.AddComponent<CircleCollider2DComponent>();
        ballCollider.Radius = 10.0f;
        ballCollider.Restitution = 0.4f;
        physicsBall.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "CollisionLogBehaviour" });
        m_Scene.SetParent(physicsBall, physics2DGroup);

        // CapsuleCollider2DComponent, plus the Bouncy.physmat above overriding its own
        // Friction/Restitution -- watch it bounce noticeably higher off PhysicsGround than the
        // plain PhysicsBall above.
        Entity physicsCapsule2D = m_Scene.CreateEntity("PhysicsCapsule2D");
        physicsCapsule2D.GetComponent<TransformComponent>().Translation = { 160.0f, 40.0f, 0.0f };
        auto& capsule2DSprite = physicsCapsule2D.AddComponent<SpriteRendererComponent>();
        capsule2DSprite.Size = { 16.0f, 32.0f };
        capsule2DSprite.Color = { 0.65f, 0.4f, 0.9f, 1.0f };
        physicsCapsule2D.AddComponent<Rigidbody2DComponent>();
        auto& capsule2DCollider = physicsCapsule2D.AddComponent<CapsuleCollider2DComponent>();
        capsule2DCollider.PhysicsMaterial = AssetRef{ bouncyMaterialGuid };
        m_Scene.SetParent(physicsCapsule2D, physics2DGroup);

        // PolygonCollider2DComponent -- default vertices form a diamond, distinct from every
        // Box/Circle/Capsule shape already demoed above.
        Entity physicsPolygon2D = m_Scene.CreateEntity("PhysicsPolygon2D");
        physicsPolygon2D.GetComponent<TransformComponent>().Translation = { 220.0f, 40.0f, 0.0f };
        auto& polygon2DSprite = physicsPolygon2D.AddComponent<SpriteRendererComponent>();
        polygon2DSprite.Size = { 16.0f, 16.0f };
        polygon2DSprite.Color = { 0.9f, 0.6f, 0.3f, 1.0f };
        physicsPolygon2D.AddComponent<Rigidbody2DComponent>();
        physicsPolygon2D.AddComponent<PolygonCollider2DComponent>();
        m_Scene.SetParent(physicsPolygon2D, physics2DGroup);

        // ---------------------------------------------------------------- Physics 3D
        Entity physics3DGroup = group("-- Physics 3D --");

        // Same idea as the 2D physics group above, but with Bullet: proves Scene's own
        // btDiscreteDynamicsWorld integration end to end -- body creation, gravity, collision
        // response, and the physics-to-Transform sync-back every frame.
        Entity physicsGround3D = m_Scene.CreateEntity("PhysicsGround3D");
        physicsGround3D.GetComponent<TransformComponent>().Translation = { 0.0f, 150.0f, 0.0f };
        physicsGround3D.GetComponent<TransformComponent>().Scale = { 200.0f, 20.0f, 200.0f };
        physicsGround3D.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Cube);
        auto& groundMesh3D = physicsGround3D.AddComponent<MeshRendererComponent>();
        groundMesh3D.Materials.push_back(AssetRef{ materialGuid }); // reuses TestOrange, just for a visible surface
        physicsGround3D.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
        physicsGround3D.AddComponent<BoxCollider3DComponent>().Size = { 100.0f, 10.0f, 100.0f };
        m_Scene.SetParent(physicsGround3D, physics3DGroup);

        Entity physicsBall3D = m_Scene.CreateEntity("PhysicsBall3D");
        physicsBall3D.GetComponent<TransformComponent>().Translation = { 80.0f, -40.0f, 0.0f };
        physicsBall3D.GetComponent<TransformComponent>().Scale = { 40.0f, 40.0f, 40.0f };
        physicsBall3D.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Sphere);
        physicsBall3D.AddComponent<MeshRendererComponent>();
        physicsBall3D.AddComponent<Rigidbody3DComponent>();
        auto& ballCollider3D = physicsBall3D.AddComponent<SphereCollider3DComponent>();
        ballCollider3D.Radius = 20.0f;
        ballCollider3D.Restitution = 0.4f;
        m_Scene.SetParent(physicsBall3D, physics3DGroup);

        // CapsuleCollider3DComponent, rendered with the real MeshPrimitive::Capsule (not a
        // stand-in Cube/Sphere) so its visual shape actually matches its collider.
        Entity physicsCapsule3D = m_Scene.CreateEntity("PhysicsCapsule3D");
        physicsCapsule3D.GetComponent<TransformComponent>().Translation = { -80.0f, -40.0f, 0.0f };
        physicsCapsule3D.GetComponent<TransformComponent>().Scale = { 40.0f, 40.0f, 40.0f }; // uniform: the capsule mesh is 1 tall, 0.5 wide
        physicsCapsule3D.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Capsule);
        physicsCapsule3D.AddComponent<MeshRendererComponent>();
        physicsCapsule3D.AddComponent<Rigidbody3DComponent>();
        physicsCapsule3D.AddComponent<CapsuleCollider3DComponent>();
        m_Scene.SetParent(physicsCapsule3D, physics3DGroup);

        // ---------------------------------------------------------------- Scripting API
        Entity scriptingGroup = group("-- Scripting API --");

        // Living documentation for the scripting API surface -- Input, SaveSystem, DateTime,
        // AudioEngine -- all exercised by ApiShowcaseBehaviour (see GameScripts/Source/
        // ApiShowcaseBehaviour.cpp). Move it with WASD/arrows (or the Circle Pad on 3DS), hold
        // the mouse/touch to pull it toward the pointer, press Space/A for a beep, watch it spin
        // once a minute driven by the real clock.
        Entity apiShowcase = m_Scene.CreateEntity("ApiShowcase");
        apiShowcase.GetComponent<TransformComponent>().Translation = { TopScreenWidth * 0.5f, 60.0f, 0.0f };
        apiShowcase.AddComponent<SpriteRendererComponent>().Size = { 32.0f, 32.0f };
        apiShowcase.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "ApiShowcaseBehaviour" });
        m_Scene.SetParent(apiShowcase, scriptingGroup);

        // Live demo of DUALITY_PROPERTY's full surface in one script -- scalars, bool, AssetRef,
        // EntityRef, a nested DUALITY_SERIALIZABLE() struct, a script-defined enum, and engine
        // enum dropdowns (Layer/BodyType) -- see GameScripts/FeatureShowcaseBehaviour.h. WASD/
        // arrows move it too; every field is tunable live in the Properties panel.
        Entity featureShowcase = m_Scene.CreateEntity("FeatureShowcase");
        featureShowcase.GetComponent<TransformComponent>().Translation = { 320.0f, 60.0f, 0.0f };
        auto& featureSprite = featureShowcase.AddComponent<SpriteRendererComponent>();
        featureSprite.Size = { 24.0f, 24.0f };
        featureShowcase.AddComponent<Rigidbody2DComponent>();
        auto& featureCollider = featureShowcase.AddComponent<CircleCollider2DComponent>();
        featureCollider.Radius = 12.0f;
        featureShowcase.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "FeatureShowcaseBehaviour" });
        m_Scene.SetParent(featureShowcase, scriptingGroup);

        // Physics Raycast API demo -- no Transform/collider of its own needed, just watches the
        // Bottom screen's pointer (BottomCamera above is Perspective, so ScreenPointToRay3D
        // works against it) and logs whichever 3D collider a click/touch hits (any entity in the
        // Physics 3D group above). See GameScripts/RaycastDemoBehaviour.cpp.
        Entity raycastController = m_Scene.CreateEntity("RaycastController");
        raycastController.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "RaycastDemoBehaviour" });
        m_Scene.SetParent(raycastController, scriptingGroup);

        // Per-project scripts demo -- ProjectDemoBehaviour lives in SampleProject/Assets/
        // Scripts/ (this PROJECT's own script, not GameScripts/'s shared ones), compiled in via
        // GameScripts/CMakeLists.txt's DUALITY_PROJECT_SCRIPTS_DIR. Logs once on Play start -- a
        // real, permanent proof this pipeline works, matching every other demo in this function.
        Entity projectScriptDemo = m_Scene.CreateEntity("ProjectScriptDemo");
        projectScriptDemo.AddComponent<BehaviourComponent>().Scripts.push_back(ScriptInstance{ "ProjectDemoBehaviour" });
        m_Scene.SetParent(projectScriptDemo, scriptingGroup);

        // ---------------------------------------------------------------- UI (Canvas)
        Entity uiWidgetsGroup = group("-- UI --");
        Entity bottomCanvas = m_Scene.CreateEntity("Bottom Canvas");
        bottomCanvas.AddComponent<CanvasComponent>().Screen = Screen::Bottom;
        m_Scene.SetParent(bottomCanvas, uiWidgetsGroup);

        // First real use of Phase 1's font rendering -- Text was previously data-only and never
        // drawn by UIRenderer.cpp at all.
        Entity demoLabel = m_Scene.CreateEntity("DemoLabel");
        auto& demoLabelRect = demoLabel.AddComponent<UIRectComponent>();
        demoLabelRect.AnchorMin = demoLabelRect.AnchorMax = demoLabelRect.Pivot = { 0.5f, 0.0f };
        demoLabelRect.AnchoredPosition = { 0.0f, 8.0f };
        demoLabelRect.SizeDelta = { 220.0f, 20.0f };
        auto& demoLabelText = demoLabel.AddComponent<UITextComponent>();
        demoLabelText.Text = "Duality Engine Demo";
        demoLabelText.FontSize = 14.0f;
        demoLabelText.Alignment = TextAlignment::Center;
        m_Scene.SetParent(demoLabel, bottomCanvas);

        Entity demoSlider = m_Scene.CreateEntity("DemoSlider");
        auto& demoSliderRect = demoSlider.AddComponent<UIRectComponent>();
        demoSliderRect.AnchorMin = demoSliderRect.AnchorMax = demoSliderRect.Pivot = { 0.0f, 0.5f };
        demoSliderRect.AnchoredPosition = { 20.0f, -30.0f };
        demoSliderRect.SizeDelta = { 100.0f, 16.0f };
        demoSlider.AddComponent<UIImageComponent>();
        demoSlider.AddComponent<UISliderComponent>();
        m_Scene.SetParent(demoSlider, bottomCanvas);

        Entity demoToggle = m_Scene.CreateEntity("DemoToggle");
        auto& demoToggleRect = demoToggle.AddComponent<UIRectComponent>();
        demoToggleRect.AnchorMin = demoToggleRect.AnchorMax = demoToggleRect.Pivot = { 0.0f, 0.5f };
        demoToggleRect.AnchoredPosition = { 20.0f, 0.0f };
        demoToggleRect.SizeDelta = { 24.0f, 24.0f };
        demoToggle.AddComponent<UIImageComponent>();
        demoToggle.AddComponent<UIToggleComponent>();
        m_Scene.SetParent(demoToggle, bottomCanvas);

        Entity demoInputField = m_Scene.CreateEntity("DemoInputField");
        auto& demoInputRect = demoInputField.AddComponent<UIRectComponent>();
        demoInputRect.AnchorMin = demoInputRect.AnchorMax = demoInputRect.Pivot = { 0.0f, 0.5f };
        demoInputRect.AnchoredPosition = { 20.0f, 30.0f };
        demoInputRect.SizeDelta = { 140.0f, 24.0f };
        demoInputField.AddComponent<UIImageComponent>();
        demoInputField.AddComponent<UIInputFieldComponent>();
        m_Scene.SetParent(demoInputField, bottomCanvas);

        // Demo Button in the Bottom screen's bottom-right corner -- its own Normal/Hover/
        // Pressed color already shows interaction feedback with no script needed; try
        // touching/clicking it.
        Entity testButton = m_Scene.CreateEntity("TestButton");
        auto& testButtonRect = testButton.AddComponent<UIRectComponent>();
        LegacyUIAnchorToRectTransform(UIAnchor::BottomRight, { 10.0f, 10.0f }, { 80.0f, 32.0f },
            testButtonRect.AnchorMin, testButtonRect.AnchorMax, testButtonRect.Pivot,
            testButtonRect.AnchoredPosition, testButtonRect.SizeDelta);
        testButton.AddComponent<UIImageComponent>();
        testButton.AddComponent<UIButtonComponent>();
        m_Scene.SetParent(testButton, bottomCanvas);

        // ---------------------------------------------------------------- Gameplay Components
        Entity gameplayGroup = group("-- Gameplay Components --");

        // ParticleSystemComponent -- no Texture assigned, so it emits flat colored quads (same
        // graceful-degradation convention as every other AssetRef in this engine).
        Entity particleDemo = m_Scene.CreateEntity("ParticleDemo");
        particleDemo.GetComponent<TransformComponent>().Translation = { 350.0f, 90.0f, 0.0f };
        particleDemo.AddComponent<ParticleSystemComponent>();
        m_Scene.SetParent(particleDemo, gameplayGroup);

        // LineRendererComponent -- Points are entity-local offsets from this Transform, a small
        // open chevron shape.
        Entity lineDemo = m_Scene.CreateEntity("LineDemo");
        lineDemo.GetComponent<TransformComponent>().Translation = { 50.0f, 150.0f, 0.0f };
        auto& line = lineDemo.AddComponent<LineRendererComponent>();
        line.PointCount = 3;
        line.Point0 = { 0.0f, 0.0f, 0.0f };
        line.Point1 = { 30.0f, -30.0f, 0.0f };
        line.Point2 = { 60.0f, 0.0f, 0.0f };
        line.Color = { 0.4f, 0.9f, 0.95f, 1.0f };
        m_Scene.SetParent(lineDemo, gameplayGroup);

        // TilemapComponent -- no Tileset/TileData assigned yet (same "component slot with no
        // asset" precedent as PhysicsBall3D's own unmaterialed mesh above), just demonstrates
        // the component exists and is reflected/editable.
        Entity tilemapDemo = m_Scene.CreateEntity("TilemapDemo");
        tilemapDemo.AddComponent<TilemapComponent>();
        m_Scene.SetParent(tilemapDemo, gameplayGroup);

        // FollowTargetComponent -- tracks PhysicsBall's fall in real time (SmoothSpeed > 0, so
        // it eases toward it rather than snapping every frame); a small marker sprite makes the
        // following motion visible without a real camera rig.
        Entity followDemo = m_Scene.CreateEntity("FollowTargetDemo");
        followDemo.GetComponent<TransformComponent>().Translation = { 100.0f, 20.0f, 0.0f };
        auto& followSprite = followDemo.AddComponent<SpriteRendererComponent>();
        followSprite.Size = { 8.0f, 8.0f };
        followSprite.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
        auto& follow = followDemo.AddComponent<FollowTargetComponent>();
        follow.Target = EntityRef{ static_cast<uint32_t>(physicsBall.Handle()) };
        follow.Offset = { 0.0f, -30.0f, 0.0f };
        follow.SmoothSpeed = 2.0f;
        follow.FollowZ = false;
        m_Scene.SetParent(followDemo, gameplayGroup);

        // LayerComponent -- replaces the old ScreenGroupComponent tag; Default (this demo's
        // value) means "ungrouped, visible on whichever screen's camera sees it by position",
        // same as every other entity in this scene that has no LayerComponent at all.
        Entity layerDemo = m_Scene.CreateEntity("LayerDemo");
        layerDemo.GetComponent<TransformComponent>().Translation = { 350.0f, 150.0f, 0.0f };
        auto& layerSprite = layerDemo.AddComponent<SpriteRendererComponent>();
        layerSprite.Size = { 12.0f, 12.0f };
        layerSprite.Color = { 0.8f, 0.8f, 0.8f, 1.0f };
        layerDemo.AddComponent<LayerComponent>().Value = Layer::Default;
        m_Scene.SetParent(layerDemo, gameplayGroup);

        // AudioSourceComponent -- PlayOnAwake is deliberately off (a beep firing on every single
        // Editor launch would get old fast); press Space/A on ApiShowcase or FeatureShowcase
        // above to actually hear a clip play through this same component type.
        Entity audioDemo = m_Scene.CreateEntity("AudioSourceDemo");
        auto& audioSource = audioDemo.AddComponent<AudioSourceComponent>();
        audioSource.Clip = AssetRef{ "8a4d16acd221a915c511d03ab1e78b16" }; // SampleProject/Assets/Audio/beep.wav
        audioSource.PlayOnAwake = false;
        m_Scene.SetParent(audioDemo, gameplayGroup);

        m_Selected = topQuad;
        m_SelectedEntities = { topQuad };
    }

    // Shared by Open Project / New Project / File > Open Recent: stops Play, swaps the active
    // project and loads its start scene.
    void Application::SwitchProject(const std::shared_ptr<Project>& project) {
        if (!project)
            return;
        if (m_IsPlaying) {
            StopAdditiveRuntimeScenes();
            m_Scene.OnRuntimeStop();
            m_IsPlaying = false;
            m_Session.IsPaused = false;
        }
        if (m_IsEditingPrefab) {
            m_IsEditingPrefab = false;
            m_EditingPrefabPath.clear();
        }

        m_Project = project;
        RenderSettings::SetShadowMode(m_Project->GetConfig().ShadowTechnique);
        m_Scene = Scene(); // old Entity handles (including m_Selected) don't survive this
        m_Selected = Entity();
        m_SelectedEntities.clear();
        m_SelectedAssetPath.clear();
        m_SelectedAssetPaths.clear();
        m_Session.Ids.Clear();
        m_Session.ChangeGroupOpen = false;
        m_Session.PendingUndoLabel.clear();
        m_TopSceneView = SceneViewCamera();       // re-seed from the new scene's own cameras
        m_BottomSceneView = SceneViewCamera();    // instead of keeping the old project's pan/zoom
        m_TopSceneView3D = SceneViewCamera3D();   // same reasoning, 3D orbit cameras
        m_BottomSceneView3D = SceneViewCamera3D();
        m_ScenePath = GetStartupScenePath(m_Project);
        m_ContentBrowserPanel.SetRootDirectory(m_Project->GetAssetsDirectory());
        AssetDatabase::Refresh(m_Project->GetAssetsDirectory());
        // Compiles+loads THIS project's own Assets/Scripts (see GameScripts/CMakeLists.txt's
        // DUALITY_PROJECT_SCRIPTS_DIR) alongside the engine's shared scripts, with no extra
        // click needed. Safe to be async here even though Deserialize runs right after --
        // SceneSerializer only stores ScriptInstance::ClassName strings, it doesn't need
        // ScriptRegistry to already know the class until Play actually starts.
        ScriptEngine::ReloadAsync(m_BuildDirectory);
        TypeScriptCompiler::CompileAsync(m_RepoRoot, m_Project->GetDirectory());

        // A brand new project has no configured start scene yet -- Deserialize logs an
        // error and leaves m_Scene empty in that case.
        SceneSerializer(m_Scene).Deserialize(m_ScenePath);
        m_SceneHistory.Reset(m_Scene, m_Session.Ids);
        m_SceneDirty = false;
        RecordRecentProject();
    }

    void Application::OpenProjectFromDialog() {
        std::string path = FileDialogs::OpenFile(m_Window.GetNativeWindow(), "Duality Project (*.dproj)\0*.dproj\0");
        if (path.empty())
            return;
        SwitchProject(Project::Load(path));
    }

    // "Create New Project" -- the chosen path's filename (minus extension) becomes both the
    // project's Name and a same-named subfolder created under its parent folder, e.g. picking
    // "F:\Projects\MyGame.dproj" creates "F:\Projects\MyGame\MyGame.dproj" +
    // "F:\Projects\MyGame\Assets\" -- matches SampleProject's own layout and mirrors how
    // Unity's "New Project" dialog asks for a location + name and creates location/name/.
    void Application::NewProjectFromDialog() {
        std::string path = FileDialogs::SaveFile(m_Window.GetNativeWindow(), "Duality Project (*.dproj)\0*.dproj\0");
        if (path.empty())
            return;

        std::filesystem::path chosen(path);
        std::string name = chosen.stem().string();
        std::string directory = (chosen.parent_path() / name).string();
        SwitchProject(Project::New(directory, name));
    }

    // Unity's "Save As": writes the current scene to a new file and makes that file the open
    // scene (later Ctrl+S saves there). Also how an untitled scene (File > New Scene) gets its
    // first file name.
    void Application::SaveSceneAsFromDialog() {
        std::string assetsDir = m_Project->GetAssetsDirectory();
        std::string path = FileDialogs::SaveFile(m_Window.GetNativeWindow(), "Duality Scene (*.scene)\0*.scene\0", assetsDir.c_str());
        if (path.empty())
            return;
        if (std::filesystem::path(path).extension().empty())
            path += ".scene";
        if (!SceneSerializer(m_Scene).Serialize(path))
            return;
        const std::string guid = AssetMeta::EnsureMetaFile(path);
        AssetDatabase::Register(guid, path);
        m_ScenePath = path;
        m_SceneHistory.MarkSaved(m_Scene, m_Session.Ids);
        m_SceneDirty = false;
    }


    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent&) {
            // Unity: closing the editor with unsaved scene changes asks first.
            if (m_SceneDirty && !m_IsPlaying && !m_Automation.IsActive()) {
                m_Window.SetShouldClose(false);
                m_QuitAfterPrompt = true;
                RunAfterSavePrompt([this](EditorContext&) { m_Running = false; });
                return true;
            }
            m_Running = false;
            return true;
        });
        // WindowResizeEvent has nothing to react to yet -- Scene/Game panels
        // size their own framebuffers off ImGui::GetContentRegionAvail(),
        // not off the OS window size -- but routing it through here (rather
        // than not having it at all) is what lets a future feature react to
        // an OS-level resize without Application needing to know GLFW exists.
    }

    // Unity's default layout: Hierarchy left, Scene/Game tabs center, Inspector right,
    // Project/Console tabs along the bottom. Built only when no saved layout exists (first
    // launch, or a dockspace that was never split) and by Window > Layouts > Reset.
    void Application::BuildDefaultDockLayout(unsigned int dockspaceId) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.24f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, nullptr, &dockMain);
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Lighting", dockRight);
        ImGui::DockBuilderDockWindow("Project", dockBottom);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        // Scene and Game share the same center dock node, so they come up as tabs.
        ImGui::DockBuilderDockWindow("Scene", dockMain);
        ImGui::DockBuilderDockWindow("Game", dockMain);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    namespace {
        // Unity's toolbar buttons: an icon toggle that stays highlighted while active.
        bool ToolbarToggle(const char* label, bool active, const char* tooltip, ImVec2 size = ImVec2(0.0f, 0.0f)) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(EditorColors::Selection()));
            }
            const bool pressed = ImGui::Button(label, size);
            if (active)
                ImGui::PopStyleColor(2);
            if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        }
    }

    // The strip under the menu bar: transform tools + handle position/rotation + snapping on the
    // left, Play / Pause / Step centered, Layout on the right -- Unity's main toolbar.
    void Application::DrawToolbar(EditorContext& ctx) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float height = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.FramePadding.x * 2.0f, style.FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        ImGui::BeginChild("##MainToolbar", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        const float button = ImGui::GetFrameHeight();
        const ImVec2 square(button * 1.25f, button);
        ImGui::SetCursorPosY((height - button) * 0.5f);

        struct Tool { const char* Icon; GizmoMode Mode; const char* Tip; };
        const Tool tools[] = {
            { EditorIcons::Hand, GizmoMode::View, "View Tool (Q)\nLeft-drag pans the Scene view" },
            { EditorIcons::Move, GizmoMode::Translate, "Move Tool (W)" },
            { EditorIcons::Rotate, GizmoMode::Rotate, "Rotate Tool (E)" },
            { EditorIcons::Scale, GizmoMode::Scale, "Scale Tool (R)" },
            { EditorIcons::RectTool, GizmoMode::Rect, "Rect Tool (T)\n2D move/resize of sprites" },
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, style.ItemSpacing.y));
        for (const Tool& tool : tools) {
            ImGui::PushID(tool.Icon);
            if (ToolbarToggle(tool.Icon, ctx.ActiveGizmoMode == tool.Mode, tool.Tip, square))
                ctx.ActiveGizmoMode = tool.Mode;
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
        const bool pivot = m_Session.ToolHandlePosition == HandlePosition::Pivot;
        if (ToolbarToggle((std::string(pivot ? EditorIcons::Crosshairs : EditorIcons::Square) + (pivot ? " Pivot" : " Center")).c_str(), false,
                "Tool Handle Position (Z)\nPivot: the active object's pivot\nCenter: the center of the selection"))
            m_Session.ToolHandlePosition = pivot ? HandlePosition::Center : HandlePosition::Pivot;
        ImGui::SameLine();
        const bool global = m_Session.ToolHandleSpace == HandleSpace::Global;
        if (ToolbarToggle((std::string(global ? EditorIcons::Globe : EditorIcons::Cube) + (global ? " Global" : " Local")).c_str(), false,
                "Tool Handle Rotation (X)\nGlobal: world axes\nLocal: the active object's rotation"))
            m_Session.ToolHandleSpace = global ? HandleSpace::Local : HandleSpace::Global;
        ImGui::SameLine();
        if (ToolbarToggle((std::string(EditorIcons::Magnet) + " " + EditorIcons::CaretDown).c_str(), false,
                "Increment Snapping (hold Ctrl while dragging a handle)"))
            ImGui::OpenPopup("SnapSettings");
        if (ImGui::BeginPopup("SnapSettings")) {
            ImGui::TextUnformatted("Increment Snapping");
            ImGui::TextDisabled("Hold Ctrl while dragging a Move/Rotate/Scale handle.");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Move", &m_Session.SnapMove, 0.01f, 0.001f, 1000.0f, "%.3f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Rotate", &m_Session.SnapRotate, 0.5f, 0.1f, 180.0f, "%.1f deg");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Scale", &m_Session.SnapScale, 0.01f, 0.001f, 100.0f, "%.3f");
            if (ImGui::Button("Reset to defaults")) {
                m_Session.SnapMove = 0.25f;
                m_Session.SnapRotate = 15.0f;
                m_Session.SnapScale = 0.1f;
            }
            ImGui::EndPopup();
        }

        // Play / Pause / Step, centered in the toolbar.
        const float groupWidth = square.x * 3.0f + 2.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, (ImGui::GetWindowWidth() - groupWidth) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, style.ItemSpacing.y));
        ImGui::BeginDisabled(m_IsEditingPrefab);
        if (ToolbarToggle(EditorIcons::Play, m_IsPlaying, m_IsPlaying ? "Stop (Ctrl+P)" : "Play (Ctrl+P)", square))
            EditorCommands::TogglePlay(ctx);
        ImGui::SameLine();
        if (ToolbarToggle(EditorIcons::Pause, m_Session.IsPaused, "Pause (Ctrl+Shift+P)", square))
            EditorCommands::TogglePause(ctx);
        ImGui::SameLine();
        if (ToolbarToggle(EditorIcons::StepForward, false, "Step (Ctrl+Alt+P)", square))
            EditorCommands::Step(ctx);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();

        // Right side: Layout menu (Unity's Layout dropdown).
        const char* layoutLabel = "Layout";
        const float layoutWidth = ImGui::CalcTextSize(layoutLabel).x + ImGui::CalcTextSize(EditorIcons::CaretDown).x + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x * 2.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - layoutWidth - style.WindowPadding.x);
        if (ImGui::Button((std::string(layoutLabel) + " " + EditorIcons::CaretDown).c_str()))
            ImGui::OpenPopup("LayoutMenu");
        if (ImGui::BeginPopup("LayoutMenu")) {
            if (ImGui::MenuItem("Default (Unity)"))
                m_Session.RequestResetLayout = true;
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }

    void Application::DrawSaveChangesPrompt(EditorContext& ctx) {
        if (m_OpenSavePrompt) {
            m_OpenSavePrompt = false;
            // Nothing to lose: run the action straight away.
            if (!m_SceneDirty) {
                auto action = std::move(m_PendingSavePromptAction);
                m_PendingSavePromptAction = nullptr;
                m_QuitAfterPrompt = false;
                if (action)
                    action(ctx);
                return;
            }
            ImGui::OpenPopup("Scene(s) Have Been Modified");
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Scene(s) Have Been Modified", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        const std::string name = m_IsEditingPrefab
            ? std::filesystem::path(m_EditingPrefabPath).filename().string()
            : (m_ScenePath.empty() ? std::string("Untitled") : std::filesystem::path(m_ScenePath).filename().string());
        ImGui::Text("Do you want to save the changes you made in:");
        ImGui::BulletText("%s", name.c_str());
        ImGui::TextDisabled("Your changes will be lost if you don't save them.");
        ImGui::Spacing();

        enum class Answer { None, Save, DontSave, Cancel } answer = Answer::None;
        const float width = 110.0f;
        if (ImGui::Button("Save", ImVec2(width, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
            answer = Answer::Save;
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(width, 0.0f)))
            answer = Answer::DontSave;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(width, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            answer = Answer::Cancel;

        if (answer != Answer::None) {
            ImGui::CloseCurrentPopup();
            auto action = std::move(m_PendingSavePromptAction);
            m_PendingSavePromptAction = nullptr;
            bool proceed = answer == Answer::DontSave;
            if (answer == Answer::Save) {
                if (m_IsPlaying)
                    EditorCommands::StopPlay(ctx);
                if (m_ScenePath.empty() && !m_IsEditingPrefab)
                    SaveSceneAsFromDialog();
                else
                    SaveScene(ctx);
                proceed = !m_SceneDirty;
            }
            if (proceed && action)
                action(ctx);
            if (!proceed)
                m_QuitAfterPrompt = false;
        }
        ImGui::EndPopup();
    }

    // Commits the change group opened by MarkSceneDirty as ONE Undo step -- but only once the
    // interaction that produced it is over (no widget / gizmo / handle still being dragged, no
    // text field still being typed into). A whole slider drag or a typed name is one step.
    void Application::CommitPendingChange(EditorContext& ctx) {
        if (m_IsPlaying || m_CapturedSceneChangeSerial == m_SceneChangeSerial)
            return;
        if (ImGui::IsAnyItemActive() || m_DraggingGizmoAxis != GizmoAxis::None)
            return;
        m_SceneHistory.Capture(m_Scene, m_Session.Ids, m_Session.PendingUndoLabel,
            m_Session.SelectionBeforeChange, EditorCommands::CaptureSelection(ctx));
        m_CapturedSceneChangeSerial = m_SceneChangeSerial;
        m_Session.ChangeGroupOpen = false;
        m_Session.PendingUndoLabel.clear();
        m_SceneDirty = !m_SceneHistory.IsAtSavedState();
    }

    void Application::UpdateWindowTitle() {
        std::string title = "DualityEditor - ";
        title += m_Project ? m_Project->GetConfig().Name : std::string("No Project");
        title += " - ";
        if (m_IsEditingPrefab)
            title += "Prefab: " + std::filesystem::path(m_EditingPrefabPath).filename().string();
        else
            title += m_ScenePath.empty() ? std::string("Untitled") : std::filesystem::path(m_ScenePath).stem().string();
        if (m_SceneDirty)
            title += "*";
        if (m_IsPlaying)
            title += m_Session.IsPaused ? "  [Paused]" : "  [Playing]";
        if (title != m_LastWindowTitle) {
            m_Window.SetTitle(title);
            m_LastWindowTitle = title;
        }
    }

    int Application::Run() {
        double lastFrameTime = m_Window.GetTime();

        // Saved layouts from before the Content Browser was renamed to Unity's "Project": keep
        // the user's docking by renaming the window entry in imgui.ini before ImGui loads it.
        if (const char* iniName = ImGui::GetIO().IniFilename) {
            std::ifstream in(iniName, std::ios::binary);
            if (in.is_open()) {
                std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                in.close();
                const std::string oldKey = "[Window][Content Browser]";
                if (text.find(oldKey) != std::string::npos && text.find("[Window][Project]") == std::string::npos) {
                    text.replace(text.find(oldKey), oldKey.size(), "[Window][Project]");
                    std::ofstream out(iniName, std::ios::binary);
                    out << text;
                }
            }
        }

        while (m_Running && !m_Window.ShouldClose()) {
            double now = m_Window.GetTime();
            float deltaTime = static_cast<float>(now - lastFrameTime);
            lastFrameTime = now;
            Profiler::BeginFrame(); // Window > Analysis > Profiler (no-op unless recording)

            // Exponential smoothing (light -- 0.1 blend) so the stats overlay
            // reads as a steady number instead of jittering with raw 1/deltaTime.
            if (deltaTime > 0.0f) {
                float instantFps = 1.0f / deltaTime;
                m_Fps = (m_Fps <= 0.0f) ? instantFps : glm::mix(m_Fps, instantFps, 0.1f);
            }

            AudioEngine::Update();

            // Finishes a background ScriptEngine::ReloadAsync build, if one just completed --
            // must run on the main thread, and early in the frame, before any panel below reads
            // ScriptRegistry (Properties panel's script fields, Add Component's "Add Script"
            // submenu) -- see ScriptEngine::ReloadAsync's own comment for why this can't happen
            // on the background thread itself (a real, reproducible crash otherwise).
            ScriptEngine::PollMainThread();
            // TypeScript: file watching + background rebuild, bundle swap outside Play.
            TypeScriptCompiler::Update(deltaTime, m_IsPlaying, m_RepoRoot, m_Project->GetDirectory());

            // Pause freezes the game; Step advances exactly one fixed frame (Unity's Step).
            const bool advanceGame = m_IsPlaying && (!m_Session.IsPaused || m_Session.StepRequested);
            const float gameDeltaTime = m_Session.StepRequested ? (1.0f / 60.0f) : deltaTime;
            m_Session.StepRequested = false;
            if (advanceGame) {
                DUALITY_PROFILE_SCOPE("PlayerLoop");
                // Before OnRuntimeUpdate, not after -- a script polling UIButtonComponent::
                // WasClicked this frame needs this frame's value already computed. Gated on
                // Play like OnRuntimeUpdate itself.
                Profiler::BeginSample("UI.Events");
                UpdateUIInteractions(m_Scene);
                UpdatePhysicsRaycasterInteractions(m_Scene);
                for (auto& loaded : m_AdditiveRuntimeScenes) {
                    UpdateUIInteractions(*loaded.Value);
                    UpdatePhysicsRaycasterInteractions(*loaded.Value);
                }
                Profiler::EndSample();
                m_Scene.OnRuntimeUpdate(gameDeltaTime);
                for (auto& loaded : m_AdditiveRuntimeScenes)
                    loaded.Value->OnRuntimeUpdate(gameDeltaTime);

                // All active worlds have now returned from OnRuntimeUpdate, so it is safe to
                // process the deferred, ordered Single/Additive/Unload queue. Additive scenes
                // intentionally remain runtime-only: Stop restores the original edit snapshot.
                for (const SceneRequest& request : SceneManager::ConsumePendingRequests()) {
                    if (request.Type == SceneRequestType::Unload) {
                        auto found = std::find_if(m_AdditiveRuntimeScenes.begin(), m_AdditiveRuntimeScenes.end(), [&](const AdditiveRuntimeScene& loaded) { return loaded.Path == request.Path; });
                        if (found != m_AdditiveRuntimeScenes.end()) {
                            found->Value->OnRuntimeStop();
                            m_AdditiveRuntimeScenes.erase(found);
                        }
                        continue;
                    }

                    if (request.Mode == LoadSceneMode::Single) {
                        StopAdditiveRuntimeScenes();
                        m_Scene.OnRuntimeStop();
                        m_Renderer.UnloadAllTextures();
                        m_Renderer.UnloadAllFonts();
                        m_Renderer3D.UnloadAllTextures();
                        m_Renderer3D.UnloadAllMeshes();
                        m_Scene = Scene();
                        m_Selected = Entity();
                        m_SelectedEntities.clear();
                        m_SelectedAssetPath.clear();
                        m_SelectedAssetPaths.clear();
                        if (SceneSerializer(m_Scene).Deserialize(m_Project->GetAssetsDirectory() + "/" + request.Path))
                            m_Scene.OnRuntimeStart();
                        continue;
                    }

                    const bool alreadyLoaded = std::any_of(m_AdditiveRuntimeScenes.begin(), m_AdditiveRuntimeScenes.end(), [&](const AdditiveRuntimeScene& loaded) { return loaded.Path == request.Path; });
                    if (alreadyLoaded)
                        continue;
                    auto loaded = std::make_unique<Scene>();
                    if (!SceneSerializer(*loaded).Deserialize(m_Project->GetAssetsDirectory() + "/" + request.Path))
                        continue;
                    loaded->OnRuntimeStart();
                    m_AdditiveRuntimeScenes.push_back({ request.Path, std::move(loaded) });
                }
            } else if (!m_IsPlaying && !m_AdditiveRuntimeScenes.empty()) {
                // Covers a Stop action taken during the previous UI frame.
                StopAdditiveRuntimeScenes();
            }
            // Console "Error Pause": any new error while playing pauses the game.
            if (m_IsPlaying) {
                const uint64_t errors = Log::GetTotalCount(LogLevel::Error);
                if (m_Session.ErrorPause && errors > m_Session.ErrorCountAtLastCheck)
                    m_Session.IsPaused = true;
                m_Session.ErrorCountAtLastCheck = errors;
            }

            Profiler::BeginSample("Render.Game");
            m_Renderer.BeginFrame();
            m_GameRenderStats = {};
            m_TopFramebuffer.Bind();
            m_GameRenderStats += RenderScreen(m_Renderer, m_Renderer3D, m_Scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f });
            if (m_IsPlaying) {
                for (auto& loaded : m_AdditiveRuntimeScenes)
                    m_GameRenderStats += RenderScreen(m_Renderer, m_Renderer3D, *loaded.Value, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f }, false);
            }
            m_TopFramebuffer.Unbind();
            m_BottomFramebuffer.Bind();
            m_GameRenderStats += RenderScreen(m_Renderer, m_Renderer3D, m_Scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f });
            if (m_IsPlaying) {
                for (auto& loaded : m_AdditiveRuntimeScenes)
                    m_GameRenderStats += RenderScreen(m_Renderer, m_Renderer3D, *loaded.Value, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f }, false);
            }
            m_BottomFramebuffer.Unbind();
            // Snapshotted here, before the Scene view's own (Editor-only) draws
            // below add to the same renderer's running total -- this is what a
            // real dual-screen render pass actually costs.
            m_GameDrawCallCount = m_Renderer.GetDrawCallCount() + m_GameRenderStats.MeshDrawCalls;
            m_Renderer.EndFrame();
            Profiler::EndSample();
            if (Profiler::IsRecording()) {
                // Rendering / Memory modules (Unity's Profiler counters).
                Profiler::SetCounter("Draw Calls", m_GameDrawCallCount);
                Profiler::SetCounter("Sprites", m_GameRenderStats.VisibleSprites);
                Profiler::SetCounter("Mesh Draw Calls", m_GameRenderStats.MeshDrawCalls);
                Profiler::SetCounter("Visible Meshes", m_GameRenderStats.VisibleMeshes);
                Profiler::SetCounter("Texture Memory (KB)", static_cast<double>(m_Renderer.GetTextureMemoryBytes()) / 1024.0);
                Profiler::SetCounter("JS Heap (KB)", static_cast<double>(JsScripting::GetMemoryStats().MallocSize) / 1024.0);
                Profiler::SetCounter("Entities", static_cast<double>(m_Scene.Registry().storage<entt::entity>().size()));
                Profiler::SetCounter("Playing", m_IsPlaying ? 1.0 : 0.0);
            }

            m_Window.BeginFrame();

            EditorContext ctx{
                m_Scene, m_Selected, m_SelectedEntities, m_SelectedAssetPath, m_SelectedAssetPaths, m_IsPlaying, m_SceneDirty, m_SceneHistory, m_SceneChangeSerial,
                m_TopSceneView, m_BottomSceneView, m_ActiveGizmoMode, m_DraggingGizmoAxis, m_DraggingGizmoScreen,
                m_TopRenderMode, m_BottomRenderMode, m_TopSceneView3D, m_BottomSceneView3D,
                m_TopSceneFramebuffer, m_BottomSceneFramebuffer, m_TopFramebuffer, m_BottomFramebuffer, m_Renderer, m_Renderer3D,
                m_Fps, m_GameDrawCallCount, m_GameRenderStats,
                m_ScenePath, m_IsEditingPrefab, m_EditingPrefabPath, m_RequestOpenPrefabPath, m_RequestExitPrefabMode,
                m_BuildDirectory, m_RepoRoot, m_PlaySnapshot, [this]() { StopAdditiveRuntimeScenes(); },
                m_RequestOpenProject, m_RequestNewProject, m_RequestSaveSceneAs, m_RequestOpenSceneDialog,
                m_RequestBrowseExternalEditor, m_RequestBrowseIcon, m_ShowBuildSettings, m_ShowProjectSettings, m_ShowPackageManager, m_ShowPreferences,
                m_Session
            };

            // One consistent selection for every panel this frame, and the "before" selection
            // of any change that starts this frame (restored by its Undo).
            EditorCommands::NormalizeSelection(ctx);
            if (!m_Session.ChangeGroupOpen)
                m_Session.FrameStartSelection = EditorCommands::CaptureSelection(ctx);

            m_Automation.Update(ctx, m_Window);
            if (m_Automation.WantsQuit())
                m_Running = false;
            // Model importer: finish background imports, watch Assets for new/changed models.
            ModelImportUI::Update(ctx);

            // Unity's play-mode tint: the whole editor darkens/reddens while the game runs, so
            // it is obvious that edits made now will be reverted on Stop.
            int tintColors = 0;
            if (m_IsPlaying) {
                const ImGuiStyle& style = ImGui::GetStyle();
                auto tint = [&](ImGuiCol color) {
                    const ImVec4 c = style.Colors[color];
                    ImGui::PushStyleColor(color, ImVec4(c.x * 0.86f + 0.04f, c.y * 0.78f, c.z * 0.78f, c.w));
                    ++tintColors;
                };
                for (ImGuiCol color : { ImGuiCol_WindowBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive,
                        ImGuiCol_Tab, ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected, ImGuiCol_FrameBg,
                        ImGuiCol_Button, ImGuiCol_Header, ImGuiCol_DockingEmptyBg })
                    tint(color);
            }

            // --- Fullscreen dockspace host: menu bar, main toolbar, dock space --------------
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

            // Global shortcuts run inside the host window so ImGui's own shortcut routing (menus,
            // popups) has already been set up for this frame.
            EditorCommands::HandleGlobalShortcuts(ctx);
            m_MenuBarPanel.OnImGuiRender(ctx);
            DrawToolbar(ctx);

            ImGuiID dockspaceId = ImGui::GetID("EditorDockspace");
            if (!m_DockLayoutInitialized || m_Session.RequestResetLayout) {
                ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
                const bool hasSavedLayout = node && (node->ChildNodes[0] || node->ChildNodes[1]);
                if (!hasSavedLayout || m_Session.RequestResetLayout) {
                    m_Session.ShowScene = m_Session.ShowGame = m_Session.ShowHierarchy = true;
                    m_Session.ShowInspector = m_Session.ShowProject = m_Session.ShowConsole = true;
                    BuildDefaultDockLayout(dockspaceId);
                }
                m_Session.RequestResetLayout = false;
                m_DockLayoutInitialized = true;
            }
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
            ImGui::End(); // EditorDockHost

            // Handled here (not inside MenuBarPanel) since opening a project / scene means
            // swapping state only Application owns -- done before the rest of this frame's
            // panels render so they immediately reflect the change. Each one that would discard
            // the open scene goes through the "save changes?" prompt first.
            if (m_RequestOpenProject) {
                m_RequestOpenProject = false;
                RunAfterSavePrompt([this](EditorContext&) { OpenProjectFromDialog(); });
            }
            if (m_RequestNewProject) {
                m_RequestNewProject = false;
                RunAfterSavePrompt([this](EditorContext&) { NewProjectFromDialog(); });
            }
            if (m_Session.RequestQuit) {
                m_Session.RequestQuit = false;
                RunAfterSavePrompt([this](EditorContext&) { m_Running = false; });
            }
            if (!m_Session.RequestOpenProjectPath.empty()) {
                const std::string path = m_Session.RequestOpenProjectPath;
                m_Session.RequestOpenProjectPath.clear();
                RunAfterSavePrompt([this, path](EditorContext&) { SwitchProject(Project::Load(path)); });
            }
            if (m_RequestSaveSceneAs) {
                m_RequestSaveSceneAs = false;
                SaveSceneAsFromDialog();
            }
            if (!m_RequestOpenPrefabPath.empty()) {
                const std::string path = m_RequestOpenPrefabPath;
                m_RequestOpenPrefabPath.clear();
                EnterPrefabMode(path);
            }
            if (m_RequestExitPrefabMode) {
                m_RequestExitPrefabMode = false;
                ExitPrefabMode();
            }
            if (m_RequestOpenSceneDialog) {
                m_RequestOpenSceneDialog = false;
                RunAfterSavePrompt([this](EditorContext& later) {
                    std::string path = FileDialogs::OpenFile(m_Window.GetNativeWindow(), "Duality Scene (*.scene)\0*.scene\0");
                    if (!path.empty())
                        OpenScene(later, path);
                });
            }
            // Same native-dialog constraint as RequestOpenSceneDialog above -- PreferencesPanel
            // itself has no window handle to call FileDialogs::OpenFile with.
            if (m_RequestBrowseExternalEditor) {
                m_RequestBrowseExternalEditor = false;
                std::string path = FileDialogs::OpenFile(m_Window.GetNativeWindow(), "Executable (*.exe)\0*.exe\0");
                if (!path.empty()) {
                    EditorSettings::Get().ExternalEditorPath = path;
                    EditorSettings::Get().Save();
                }
            }
            // Same native-dialog constraint as the two blocks above -- ProjectSettingsPanel has
            // no window handle of its own. Per-project (Project::GetConfig().IconPath).
            if (m_RequestBrowseIcon) {
                m_RequestBrowseIcon = false;
                std::string path = FileDialogs::OpenFile(m_Window.GetNativeWindow(), "PNG Image (*.png)\0*.png\0");
                if (!path.empty() && m_Project) {
                    m_Project->GetConfig().IconPath = path;
                    m_Project->Save();
                }
            }

            Profiler::BeginSample("Editor.Panels");
            m_ScenePanel.OnImGuiRender(ctx);
            m_GamePanel.OnImGuiRender(ctx);
            m_HierarchyPanel.OnImGuiRender(ctx);
            m_PropertiesPanel.OnImGuiRender(ctx);
            m_ContentBrowserPanel.OnImGuiRender(ctx);
            m_ConsolePanel.OnImGuiRender(ctx);
            m_BuildSettingsPanel.OnImGuiRender(ctx);
            m_ProjectSettingsPanel.OnImGuiRender(ctx);
            m_PackageManagerPanel.OnImGuiRender(ctx);
            m_PreferencesPanel.OnImGuiRender(ctx);
            m_LightingPanel.OnImGuiRender(ctx);
            m_SpriteEditorWindow.OnImGuiRender(ctx);
            m_AnimationWindow.OnImGuiRender(ctx);
            m_TilePaletteWindow.OnImGuiRender(ctx);
            m_ProfilerWindow.OnImGuiRender(ctx);
            Profiler::EndSample();

            DrawSaveChangesPrompt(ctx);

            if (tintColors > 0)
                ImGui::PopStyleColor(tintColors);

            // MarkSceneDirty is called by every Scene mutation path (Inspector, gizmo,
            // hierarchy, creation/deletion, drag-drop). One finished interaction becomes one
            // atomic history entry. An Animation-window preview is suspended around it so the
            // history never captures previewed (unsaved, sampled) values.
            const bool suspendPreview = m_Session.ScenePreviewHook && m_CapturedSceneChangeSerial != m_SceneChangeSerial;
            if (suspendPreview)
                m_Session.ScenePreviewHook(true);
            CommitPendingChange(ctx);
            if (suspendPreview && m_Session.ScenePreviewHook)
                m_Session.ScenePreviewHook(false);
            UpdateWindowTitle();

            Profiler::EndFrame(); // before the buffer swap (vsync wait is not frame work)
            m_Window.EndFrame();
        }

        if (m_IsPlaying) {
            StopAdditiveRuntimeScenes();
            m_Scene.OnRuntimeStop();
        }

        ScriptEngine::Shutdown();
        JsScripting::Unload();
        m_Renderer.Shutdown();
        m_Renderer3D.Shutdown();
        AudioEngine::Shutdown();
        return m_Automation.IsActive() ? m_Automation.FailureCount() : 0;
    }

}
