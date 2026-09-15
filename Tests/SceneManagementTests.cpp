// Scene::Clear() + SceneSerializer's in-memory JSON round trip -- the primitives behind the
// Editor's "Load Scene" bug fix (used to ADD loaded entities into whatever was already there
// instead of replacing them) and the Play->Stop scene-state snapshot/restore (GamePanel.cpp:
// leaving Play mode now reverts every change Play made, matching Unity). The ImGui-level
// button/double-click behavior itself isn't testable headlessly, but everything it's built on
// (Scene::Clear, SerializeToJson, DeserializeFromJson) is.
#include "TestFramework.h"

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scripting/MeshRenderer.h"

using namespace Duality;

namespace {
    // RenderDirectionalBlobShadows is scene traversal/policy, so test it without an OpenGL
    // context by recording the platform-neutral submissions it makes to IRenderer3D.
    class RecordingRenderer3D final : public IRenderer3D {
    public:
        void Init() override {}
        void Shutdown() override {}
        void BeginScene(Screen, ProjectionType, const glm::vec3&, const glm::vec3&, float, float, float, float, float, const glm::vec4&, bool) override {}
        void BeginScene(const RenderView&, const glm::vec4&, bool) override {}
        void EndScene() override {}
        void DrawMesh(MeshPrimitive, uint32_t, uint32_t, const glm::vec3&, const glm::vec3&, const glm::vec3&, const glm::vec4&, uint32_t) override {}
        void DrawMesh(const MeshDrawCommand& command) override { Commands.push_back(command); }
        uint32_t GetSubMeshCount(uint32_t) const override { return 1; }
        uint32_t LoadTexture(const std::string&) override { return 0; }
        uint32_t LoadMesh(const std::string&) override { return 0; }
        void UnloadAllTextures() override {}
        void UnloadAllMeshes() override {}
        uint32_t GetDrawCallCount() const override { return static_cast<uint32_t>(Commands.size()); }

        std::vector<MeshDrawCommand> Commands;
    };
}

TEST_CASE("Scene::Clear() removes every entity and resets the root list") {
    Scene scene;
    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, parent);
    scene.CreateEntity("Loose");

    CHECK(scene.GetRootEntities().size() == 2); // Parent, Loose (Child is Parent's own child)

    scene.Clear();

    CHECK_SOFT(scene.GetRootEntities().empty(), "root list is reset, not left holding dangling handles");
    CHECK_SOFT(scene.Registry().view<NameComponent>().size() == 0, "no entities survive");

    // The registry must still be usable afterward, not just emptied -- a Clear() that left
    // the entt pools in a bad state would only surface once something tried to create again.
    Entity fresh = scene.CreateEntity("Fresh");
    CHECK_SOFT(fresh.GetComponent<NameComponent>().Name == "Fresh", "Scene is fully usable again after Clear()");
    CHECK_SOFT(scene.GetRootEntities().size() == 1, "new entity re-populates the root list normally");
}

TEST_CASE("SceneSerializer::SerializeToJson/DeserializeFromJson round-trip in memory, no disk I/O") {
    Scene source;
    Entity player = source.CreateEntity("Player");
    player.GetComponent<TransformComponent>().Translation = { 12.0f, -5.0f, 0.0f };
    Entity child = source.CreateEntity("Weapon");
    source.SetParent(child, player);

    nlohmann::json snapshot = SceneSerializer(source).SerializeToJson();

    // Mutate the live scene the same way Play would (moving an entity, adding a new one) --
    // simulates what GamePanel's "Stop" needs to revert.
    player.GetComponent<TransformComponent>().Translation = { 999.0f, 999.0f, 0.0f };
    source.CreateEntity("SpawnedDuringPlay");
    CHECK(source.GetRootEntities().size() == 2); // Player, SpawnedDuringPlay

    source.Clear();
    CHECK(SceneSerializer(source).DeserializeFromJson(snapshot));

    Entity restoredPlayer = source.FindEntityInScreen(Screen::Top, "Player");
    CHECK(restoredPlayer);
    CHECK_SOFT(restoredPlayer.GetComponent<TransformComponent>().Translation.x == 12.0f, "restored to the snapshot's value, not the mutated one");
    CHECK_SOFT(!source.FindEntityInScreen(Screen::Top, "SpawnedDuringPlay"), "entity created after the snapshot does not survive the restore");
    CHECK_SOFT(source.GetRootEntities().size() == 1, "only Player is a root again (Weapon is its child)");

    Entity restoredWeapon = source.FindEntityInScreen(Screen::Top, "Weapon");
    CHECK(restoredWeapon);
    CHECK_SOFT(restoredWeapon.GetComponent<HierarchyComponent>().Parent == restoredPlayer, "parent/child relationship survives the round trip");
}

TEST_CASE("DeserializeFromJson fails gracefully on a JSON object with no \"Entities\" key") {
    Scene scene;
    CHECK_SOFT(!SceneSerializer(scene).DeserializeFromJson(nlohmann::json::object()), "malformed snapshot is rejected, not silently accepted");
}

TEST_CASE("DirectionalLightComponent round-trips through a scene snapshot") {
    Scene source;
    Entity light = source.CreateEntity("Sun");
    auto& authored = light.AddComponent<DirectionalLightComponent>();
    authored.Color = { 0.25f, 0.5f, 0.75f };
    authored.Intensity = 2.0f;
    authored.CastShadows = true;

    nlohmann::json snapshot = SceneSerializer(source).SerializeToJson();
    Scene loaded;
    CHECK(SceneSerializer(loaded).DeserializeFromJson(snapshot));

    Entity restored = loaded.FindEntityInScreen(Screen::Top, "Sun");
    CHECK(restored && restored.HasComponent<DirectionalLightComponent>());
    const auto& result = restored.GetComponent<DirectionalLightComponent>();
    CHECK_SOFT(result.Color.r == 0.25f && result.Color.g == 0.5f && result.Color.b == 0.75f, "light color survives serialization");
    CHECK_SOFT(result.Intensity == 2.0f && result.CastShadows, "light intensity and shadow reservation survive serialization");
}

TEST_CASE("RenderView selects the first enabled directional light and preserves camera data") {
    Scene scene;
    Entity camera = scene.CreateEntity("Camera");
    auto& cameraComponent = camera.AddComponent<CameraComponent>();
    cameraComponent.Screen = Screen::Top;
    cameraComponent.Primary = true;
    cameraComponent.Projection = ProjectionType::Perspective;
    cameraComponent.FovDegrees = 70.0f;
    camera.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };

    Entity disabled = scene.CreateEntity("Disabled light");
    disabled.AddComponent<DirectionalLightComponent>().Enabled = false;
    Entity sun = scene.CreateEntity("Sun");
      auto& sunData = sun.AddComponent<DirectionalLightComponent>();
      sunData.Color = { 0.2f, 0.4f, 0.8f };
      sunData.Intensity = 1.5f;
      sunData.CastShadows = true;

    RenderView view = BuildRenderView(scene, camera, Screen::Top);
    CHECK_SOFT(view.Projection == ProjectionType::Perspective && view.FovDegrees == 70.0f, "RenderView copies the camera projection settings");
    CHECK_SOFT(view.CameraPosition == glm::vec3(1.0f, 2.0f, 3.0f), "RenderView copies the camera world position");
    CHECK_SOFT(view.MainLight.Enabled, "first enabled directional light is selected");
      CHECK_SOFT(view.MainLight.Color == sunData.Color && view.MainLight.Intensity == 1.5f, "selected main light preserves its authored parameters");
      CHECK_SOFT(view.MainLight.CastShadows, "selected main light preserves its Cast Shadows setting");
      CHECK_SOFT(view.MainLight.Direction.z > 0.99f, "zero-rotation directional light points from the surface toward positive Z");
  }

TEST_CASE("Directional blob shadows submit a transparent depth-read-only plane onto a Plane receiver") {
    Scene scene;
    Entity receiver = scene.CreateEntity("Ground");
    auto& receiverMesh = receiver.AddComponent<MeshRendererComponent>();
    receiverMesh.Primitive = MeshPrimitive::Plane;
    receiver.GetComponent<TransformComponent>().Scale = { 20.0f, 1.0f, 20.0f };

    Entity caster = scene.CreateEntity("Caster");
    caster.AddComponent<MeshRendererComponent>().Primitive = MeshPrimitive::Cube;
    caster.GetComponent<TransformComponent>().Translation = { 1.0f, 4.0f, -2.0f };
    caster.GetComponent<TransformComponent>().Scale = { 2.0f, 2.0f, 2.0f };

    RenderView view;
    view.MainLight.Enabled = true;
    view.MainLight.CastShadows = true;
    view.MainLight.Direction = { 0.0f, 1.0f, 0.0f }; // point-to-light; projection travels down.
    RecordingRenderer3D renderer;
    RenderDirectionalBlobShadows(renderer, scene, Screen::Top, view);

    CHECK_SOFT(renderer.Commands.size() == 1, "one cube/one Plane receiver produces one shadow command");
    const MeshDrawCommand& shadow = renderer.Commands.front();
    CHECK_SOFT(shadow.Primitive == MeshPrimitive::Plane && shadow.AlphaBlend && !shadow.DepthWrite, "shadow is a transparent, depth-read-only Plane draw");
    CHECK_SOFT(shadow.Translation.y > 0.0f && shadow.Translation.y < 0.1f, "shadow is offset just above the receiver to avoid z-fighting");
}

TEST_CASE("MeshRenderer runtime material color is per-renderer, non-serialized, and reaches the mesh pass") {
    Scene scene;
    Entity camera = scene.CreateEntity("Camera");
    auto& cameraData = camera.AddComponent<CameraComponent>();
    cameraData.Screen = Screen::Top;
    cameraData.Primary = true;

    Entity cube = scene.CreateEntity("Cube");
    cube.AddComponent<MeshRendererComponent>().Primitive = MeshPrimitive::Cube;
    MeshRenderer scriptingApi(cube);
    const glm::vec4 animatedColor{ 0.1f, 0.8f, 0.3f, 0.65f };
    scriptingApi.SetMaterialColor(animatedColor);

    CHECK_SOFT(scriptingApi.HasMaterialColor(), "the scripting facade stores a color override on this renderer");
    CHECK_SOFT(scriptingApi.GetMaterialColor() == animatedColor, "the scripting facade returns the assigned runtime color");
    CHECK_SOFT(SceneSerializer(scene).SerializeToJson().dump().find("RuntimeMaterialColor") == std::string::npos,
        "runtime material color is not authored scene data");

    RecordingRenderer3D renderer;
    RenderScreen3D(renderer, scene, Screen::Top, { 0.0f, 0.0f, 0.0f, 1.0f });
    CHECK_SOFT(renderer.Commands.size() == 1, "the cube emits one mesh command");
    CHECK_SOFT(renderer.Commands.front().Color == animatedColor,
        "SceneRenderer replaces only the material Color with the per-renderer runtime override");

    scriptingApi.ClearMaterialColor();
    CHECK_SOFT(!scriptingApi.HasMaterialColor(), "the override can be cleared when a Behaviour is destroyed");
}
