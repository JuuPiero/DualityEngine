// SceneSerializer (whole-scene save/load) regression coverage -- exercises the same
// EntitySerialization.cpp logic PrefabSerializer shares, but for a whole Scene::Serialize/
// Deserialize round trip instead of one subtree.
#include "TestFramework.h"

#include <filesystem>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;

namespace {
    std::string TempScenePath() {
        return (std::filesystem::temp_directory_path() / "duality_engine_test_scene.scene").string();
    }
}

TEST_CASE("SceneSerializer round-trips multiple entities and a parent/child link") {
    std::string path = TempScenePath();

    Scene sourceScene;
    Entity camera = sourceScene.CreateEntity("MainCamera");
    camera.AddComponent<CameraComponent>().Zoom = 2.0f;

    Entity player = sourceScene.CreateEntity("Player");
    player.GetComponent<TransformComponent>().Translation = { 5.0f, 6.0f, 0.0f };
    player.AddComponent<Rigidbody2DComponent>().IsStatic = false;
    player.AddComponent<CircleCollider2DComponent>().Radius = 12.0f;

    Entity weapon = sourceScene.CreateEntity("Weapon");
    sourceScene.SetParent(weapon, player);

    CHECK(SceneSerializer(sourceScene).Serialize(path));

    Scene loadedScene;
    CHECK(SceneSerializer(loadedScene).Deserialize(path));

    Entity loadedCamera = loadedScene.FindEntityInScreen(Screen::Top, "MainCamera");
    CHECK_SOFT(static_cast<bool>(loadedCamera), "camera entity was found by name after loading");
    if (loadedCamera)
        CHECK_SOFT(loadedCamera.GetComponent<CameraComponent>().Zoom == 2.0f, "CameraComponent field round-tripped");

    Entity loadedPlayer = loadedScene.FindEntityInScreen(Screen::Top, "Player");
    CHECK_SOFT(static_cast<bool>(loadedPlayer), "player entity was found by name after loading");
    if (loadedPlayer) {
        CHECK_SOFT(loadedPlayer.GetComponent<TransformComponent>().Translation.x == 5.0f, "Transform round-tripped");
        CHECK_SOFT(loadedPlayer.HasComponent<Rigidbody2DComponent>(), "Rigidbody2DComponent round-tripped");
        CHECK_SOFT(loadedPlayer.HasComponent<CircleCollider2DComponent>(), "CircleCollider2DComponent round-tripped");
        CHECK_SOFT(loadedPlayer.GetComponent<CircleCollider2DComponent>().Radius == 12.0f, "collider field round-tripped");

        auto& children = loadedPlayer.GetComponent<HierarchyComponent>().Children;
        CHECK_SOFT(children.size() == 1, "player has exactly one child after loading");
        if (children.size() == 1)
            CHECK_SOFT(children[0].GetComponent<NameComponent>().Name == "Weapon", "child (Weapon) round-tripped under its parent");
    }

    std::filesystem::remove(path);
}

TEST_CASE("SceneSerializer::Deserialize fails gracefully for a missing file") {
    Scene scene;
    CHECK_SOFT(!SceneSerializer(scene).Deserialize("this_scene_file_does_not_exist.scene"), "Deserialize returns false for a missing file");
}
