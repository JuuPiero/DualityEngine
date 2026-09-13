#include "DualityEditor/Application.h"

#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* -- for the initial Unity/Cocos-Creator-style dock layout

#include "DualityEditor/AssetInspectors.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorSettings.h"
#include "DualityEditor/FileDialogs.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/PhysicsMaterialLoader.h"
#include "DualityEngine/Audio/AudioEngine.h"
#include "DualityEngine/Reflection/Reflection.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/PhysicsRaycaster.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/SceneManager.h"
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

        m_Renderer.Init();
        m_Renderer3D.Init();
        AudioEngine::Init();

        m_ScenePath = m_Project->GetAssetsDirectory() + "/Scene.scene";
        // m_TopSceneView/m_BottomSceneView start unseeded -- ScenePanel seeds each
        // from that screen's real primary CameraComponent on its first render.
        AssetDatabase::Refresh(m_Project->GetAssetsDirectory());

        SetupDemoScene();
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
        auto& testCubeMesh = testCube.AddComponent<MeshRendererComponent>();
        testCubeMesh.Primitive = MeshPrimitive::Cube;
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
        auto& groundMesh3D = physicsGround3D.AddComponent<MeshRendererComponent>();
        groundMesh3D.Primitive = MeshPrimitive::Cube;
        groundMesh3D.Materials.push_back(AssetRef{ materialGuid }); // reuses TestOrange, just for a visible surface
        physicsGround3D.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
        physicsGround3D.AddComponent<BoxCollider3DComponent>().Size = { 100.0f, 10.0f, 100.0f };
        m_Scene.SetParent(physicsGround3D, physics3DGroup);

        Entity physicsBall3D = m_Scene.CreateEntity("PhysicsBall3D");
        physicsBall3D.GetComponent<TransformComponent>().Translation = { 80.0f, -40.0f, 0.0f };
        physicsBall3D.GetComponent<TransformComponent>().Scale = { 40.0f, 40.0f, 40.0f };
        physicsBall3D.AddComponent<MeshRendererComponent>().Primitive = MeshPrimitive::Sphere;
        physicsBall3D.AddComponent<Rigidbody3DComponent>();
        auto& ballCollider3D = physicsBall3D.AddComponent<SphereCollider3DComponent>();
        ballCollider3D.Radius = 20.0f;
        ballCollider3D.Restitution = 0.4f;
        m_Scene.SetParent(physicsBall3D, physics3DGroup);

        // CapsuleCollider3DComponent, rendered with the real MeshPrimitive::Capsule (not a
        // stand-in Cube/Sphere) so its visual shape actually matches its collider.
        Entity physicsCapsule3D = m_Scene.CreateEntity("PhysicsCapsule3D");
        physicsCapsule3D.GetComponent<TransformComponent>().Translation = { -80.0f, -40.0f, 0.0f };
        physicsCapsule3D.GetComponent<TransformComponent>().Scale = { 20.0f, 40.0f, 20.0f };
        physicsCapsule3D.AddComponent<MeshRendererComponent>().Primitive = MeshPrimitive::Capsule;
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
        m_TopSceneView = SceneViewCamera();       // re-seed from the new scene's own cameras
        m_BottomSceneView = SceneViewCamera();    // instead of keeping the old project's pan/zoom
        m_TopSceneView3D = SceneViewCamera3D();   // same reasoning, 3D orbit cameras
        m_BottomSceneView3D = SceneViewCamera3D();
        m_ScenePath = m_Project->GetAssetsDirectory() + "/Scene.scene";
        m_ContentBrowserPanel.SetRootDirectory(m_Project->GetAssetsDirectory());
        AssetDatabase::Refresh(m_Project->GetAssetsDirectory());
        // Compiles+loads THIS project's own Assets/Scripts (see GameScripts/CMakeLists.txt's
        // DUALITY_PROJECT_SCRIPTS_DIR) alongside the engine's shared scripts, with no extra
        // click needed. Safe to be async here even though Deserialize runs right after --
        // SceneSerializer only stores ScriptInstance::ClassName strings, it doesn't need
        // ScriptRegistry to already know the class until Play actually starts.
        ScriptEngine::ReloadAsync(m_BuildDirectory);

        // A brand new project has no Scene.scene yet -- Deserialize logs an
        // error and leaves m_Scene empty in that case, same as clicking
        // "Load Scene" against a project that hasn't saved one yet.
        SceneSerializer(m_Scene).Deserialize(m_ScenePath);
    }

    // "Create New Project" -- Project::New(directory, name) already existed and does
    // everything needed (creates the directory + Assets/, writes the .dproj) but was
    // never wired up to any Editor UI. Reuses FileDialogs::SaveFile (the same native dialog
    // "Save Scene As..." already uses) instead of adding a new folder-picker dialog: the
    // chosen path's filename (minus extension) becomes both the project's Name and a
    // same-named subfolder created under its parent folder, e.g. picking
    // "F:\Projects\MyGame.dproj" creates "F:\Projects\MyGame\MyGame.dproj" +
    // "F:\Projects\MyGame\Assets\" -- matches SampleProject's own existing layout (a
    // dedicated project folder with the .dproj at its own root) and mirrors how Unity's own
    // "New Project" dialog asks for a location + name and creates location/name/ as the root.
    void Application::NewProjectFromDialog() {
        std::string path = FileDialogs::SaveFile(m_Window.GetNativeWindow(), "Duality Project (*.dproj)\0*.dproj\0");
        if (path.empty())
            return;

        std::filesystem::path chosen(path);
        std::string name = chosen.stem().string();
        std::string directory = (chosen.parent_path() / name).string();

        auto project = Project::New(directory, name);
        if (!project)
            return;

        if (m_IsPlaying) {
            m_Scene.OnRuntimeStop();
            m_IsPlaying = false;
        }

        m_Project = project;
        m_Scene = Scene(); // old Entity handles (including m_Selected) don't survive this
        m_Selected = Entity();
        m_TopSceneView = SceneViewCamera();       // re-seed from the new scene's own cameras
        m_BottomSceneView = SceneViewCamera();    // instead of keeping the old project's pan/zoom
        m_TopSceneView3D = SceneViewCamera3D();   // same reasoning, 3D orbit cameras
        m_BottomSceneView3D = SceneViewCamera3D();
        m_ScenePath = m_Project->GetAssetsDirectory() + "/Scene.scene";
        m_ContentBrowserPanel.SetRootDirectory(m_Project->GetAssetsDirectory());
        AssetDatabase::Refresh(m_Project->GetAssetsDirectory());
        // Same reasoning as OpenProjectFromDialog's own call -- compiles+loads this brand new
        // project's (empty, until Content Browser's "Create > Script" is used) Assets/Scripts.
        ScriptEngine::ReloadAsync(m_BuildDirectory);

        // Always a no-op in practice (a brand new project's Assets/ is empty) -- kept for the
        // exact same reason OpenProjectFromDialog keeps its own call: logs a normal "could not
        // open" error and leaves m_Scene empty rather than needing a special case here.
        SceneSerializer(m_Scene).Deserialize(m_ScenePath);
    }

    // Writes the CURRENT scene's content to a new file the user picks (defaulting to the
    // project's own Assets folder), WITHOUT changing m_ScenePath -- unlike Unity's own
    // "Save Scene As...", the newly-written file does not become "the" active scene; this
    // exists specifically so a project can accumulate additional scene files (e.g.
    // "Level2.scene") for Behaviour::LoadScene to target, while "Save Scene"/"Load Scene"
    // keep operating on the original scene exactly as before. A snapshot/export, not a
    // scene switch.
    void Application::SaveSceneAsFromDialog() {
        std::string assetsDir = m_Project->GetAssetsDirectory();
        std::string path = FileDialogs::SaveFile(m_Window.GetNativeWindow(), "Duality Scene (*.scene)\0*.scene\0", assetsDir.c_str());
        if (path.empty())
            return;
        SceneSerializer(m_Scene).Serialize(path);
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

            if (m_IsPlaying) {
                // Before OnRuntimeUpdate, not after -- a script polling UIButtonComponent::
                // WasClicked this frame needs this frame's value already computed. Gated on
                // Play like OnRuntimeUpdate itself -- a button's click state is gameplay state,
                // same as Unity's own UI only receiving click events at Play time, not while
                // editing the Scene view.
                UpdateUIInteractions(m_Scene);
                UpdatePhysicsRaycasterInteractions(m_Scene);
                m_Scene.OnRuntimeUpdate(deltaTime);

                // A script-requested SceneManager.LoadScene is a deferred request (see
                // SceneManager.h's own comment) -- safe to act on now, right after
                // OnRuntimeUpdate returns. Stays in Play mode (m_IsPlaying untouched) --
                // this is a scene-to-scene transition during Play, not a Stop. m_Selected
                // is reset since the old Scene's entity handles don't survive the swap,
                // same stale-handle precedent as OpenProjectFromDialog above.
                if (SceneManager::HasPendingLoad()) {
                    std::string pendingPath = SceneManager::ConsumePendingLoad();
                    m_Scene.OnRuntimeStop();
                    m_Renderer.UnloadAllTextures();
                    m_Renderer.UnloadAllFonts();
                    m_Renderer3D.UnloadAllTextures();
                    m_Renderer3D.UnloadAllMeshes();
                    m_Scene = Scene();
                    m_Selected = Entity();
                    SceneSerializer(m_Scene).Deserialize(m_Project->GetAssetsDirectory() + "/" + pendingPath);
                    m_Scene.OnRuntimeStart();
                }
            }

            m_Renderer.BeginFrame();
            m_TopFramebuffer.Bind();
            RenderScreen(m_Renderer, m_Renderer3D, m_Scene, Screen::Top, { 0.08f, 0.08f, 0.12f, 1.0f });
            m_TopFramebuffer.Unbind();
            m_BottomFramebuffer.Bind();
            RenderScreen(m_Renderer, m_Renderer3D, m_Scene, Screen::Bottom, { 0.12f, 0.08f, 0.08f, 1.0f });
            m_BottomFramebuffer.Unbind();
            // Snapshotted here, before the Scene view's own (Editor-only) draws
            // below add to the same renderer's running total -- this is what a
            // real dual-screen render pass actually costs.
            m_GameDrawCallCount = m_Renderer.GetDrawCallCount();
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
                ImGui::DockBuilderDockWindow("Inspector", dockRight);
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
                m_Scene, m_Selected, m_SelectedAssetPath, m_IsPlaying, m_SceneDirty,
                m_TopSceneView, m_BottomSceneView, m_ActiveGizmoMode, m_DraggingGizmoAxis, m_DraggingGizmoScreen,
                m_TopRenderMode, m_BottomRenderMode, m_TopSceneView3D, m_BottomSceneView3D,
                m_TopSceneFramebuffer, m_BottomSceneFramebuffer, m_TopFramebuffer, m_BottomFramebuffer, m_Renderer, m_Renderer3D,
                m_Fps, m_GameDrawCallCount,
                m_ScenePath, m_BuildDirectory, m_RepoRoot, m_PlaySnapshot,
                m_RequestOpenProject, m_RequestNewProject, m_RequestSaveSceneAs, m_RequestOpenSceneDialog,
                m_RequestBrowseExternalEditor, m_RequestBrowseIcon, m_ShowBuildSettings, m_ShowProjectSettings, m_ShowPreferences
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
            if (m_RequestNewProject) {
                m_RequestNewProject = false;
                NewProjectFromDialog();
            }
            if (m_RequestSaveSceneAs) {
                m_RequestSaveSceneAs = false;
                SaveSceneAsFromDialog();
            }
            // Inlined rather than a separate Application method (unlike OpenProjectFromDialog/
            // SaveSceneAsFromDialog above) -- this is the one action that both needs
            // m_Window.GetNativeWindow() (only Application has it) AND wants to reuse
            // SceneOps.h's OpenScene(EditorContext&, path) (which every other "open a known
            // scene path" call site already shares) -- `ctx` right here is the only place
            // both are available together without constructing a second EditorContext.
            if (m_RequestOpenSceneDialog) {
                m_RequestOpenSceneDialog = false;
                std::string path = FileDialogs::OpenFile(m_Window.GetNativeWindow(), "Duality Scene (*.scene)\0*.scene\0");
                if (!path.empty())
                    OpenScene(ctx, path);
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
            // no window handle of its own. Per-project (Project::GetConfig().IconPath +
            // Save()), not EditorSettings -- an icon belongs to the project being built, not
            // this machine's Editor install.
            if (m_RequestBrowseIcon) {
                m_RequestBrowseIcon = false;
                std::string path = FileDialogs::OpenFile(m_Window.GetNativeWindow(), "PNG Image (*.png)\0*.png\0");
                if (!path.empty() && m_Project) {
                    m_Project->GetConfig().IconPath = path;
                    m_Project->Save();
                }
            }

            m_ScenePanel.OnImGuiRender(ctx);
            m_GamePanel.OnImGuiRender(ctx);
            m_HierarchyPanel.OnImGuiRender(ctx);
            m_PropertiesPanel.OnImGuiRender(ctx);
            m_ContentBrowserPanel.OnImGuiRender(ctx);
            m_ConsolePanel.OnImGuiRender();
            m_BuildSettingsPanel.OnImGuiRender(ctx);
            m_ProjectSettingsPanel.OnImGuiRender(ctx);
            m_PreferencesPanel.OnImGuiRender(ctx);

            m_Window.EndFrame();
        }

        if (m_IsPlaying)
            m_Scene.OnRuntimeStop();

        ScriptEngine::Shutdown();
        m_Renderer.Shutdown();
        m_Renderer3D.Shutdown();
        AudioEngine::Shutdown();
    }

}
