// Prefab (PrefabSerializer::Save/Instantiate) regression coverage.
#include "TestFramework.h"

#include <cstdio>
#include <filesystem>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"

using namespace Duality;

namespace {
    // A real temp file on disk -- PrefabSerializer's own contract is file-based (same as
    // SceneSerializer), so this exercises the actual save-then-load round trip rather than
    // an in-memory shortcut.
    std::string TempPrefabPath() {
        return (std::filesystem::temp_directory_path() / "duality_engine_test_prefab.prefab").string();
    }
}

TEST_CASE("Prefab save+instantiate round-trips a single entity's components") {
    std::string path = TempPrefabPath();

    Scene sourceScene;
    Entity original = sourceScene.CreateEntity("Coin");
    original.GetComponent<TransformComponent>().Translation = { 10.0f, 20.0f, 30.0f };
    original.GetComponent<TagComponent>().Tag = "Collectible";
    auto& sprite = original.AddComponent<SpriteRendererComponent>();
    sprite.Color = { 0.9f, 0.8f, 0.1f, 1.0f };
    sprite.Size = { 24.0f, 24.0f };

    CHECK(PrefabSerializer::Save(original, path));

    Scene targetScene;
    Entity instance = PrefabSerializer::Instantiate(targetScene, path);
    CHECK(static_cast<bool>(instance));

    CHECK_SOFT(instance.GetComponent<NameComponent>().Name == "Coin", "Name round-tripped");
    CHECK_SOFT(instance.GetComponent<TagComponent>().Tag == "Collectible", "Tag round-tripped");
    const auto& t = instance.GetComponent<TransformComponent>().Translation;
    CHECK_SOFT(t.x == 10.0f && t.y == 20.0f && t.z == 30.0f, "Transform round-tripped");
    CHECK_SOFT(instance.HasComponent<SpriteRendererComponent>(), "SpriteRendererComponent round-tripped");
    CHECK_SOFT(instance.GetComponent<SpriteRendererComponent>().Size.x == 24.0f, "Sprite field values round-tripped");

    // Instantiating is a real copy, not a reference to the original scene's entity.
    original.GetComponent<TransformComponent>().Translation.x = 999.0f;
    CHECK_SOFT(instance.GetComponent<TransformComponent>().Translation.x == 10.0f, "instance is independent of the original entity");

    std::filesystem::remove(path);
}

TEST_CASE("Prefab save+instantiate preserves a parent/child subtree") {
    std::string path = TempPrefabPath();

    Scene sourceScene;
    Entity root = sourceScene.CreateEntity("Enemy");
    Entity weapon = sourceScene.CreateEntity("Weapon");
    Entity muzzle = sourceScene.CreateEntity("MuzzleFlash");
    sourceScene.SetParent(weapon, root);
    sourceScene.SetParent(muzzle, weapon);

    CHECK(PrefabSerializer::Save(root, path));

    Scene targetScene;
    Entity instanceRoot = PrefabSerializer::Instantiate(targetScene, path);
    CHECK(static_cast<bool>(instanceRoot));

    CHECK_SOFT(instanceRoot.GetComponent<NameComponent>().Name == "Enemy", "root name round-tripped");
    auto& rootChildren = instanceRoot.GetComponent<HierarchyComponent>().Children;
    CHECK_SOFT(rootChildren.size() == 1, "root has exactly one child in the instantiated copy");
    if (rootChildren.size() == 1) {
        Entity instanceWeapon = rootChildren[0];
        CHECK_SOFT(instanceWeapon.GetComponent<NameComponent>().Name == "Weapon", "child name round-tripped");
        CHECK_SOFT(instanceWeapon.GetComponent<HierarchyComponent>().Parent == instanceRoot, "child's Parent points back to the new root");
        auto& weaponChildren = instanceWeapon.GetComponent<HierarchyComponent>().Children;
        CHECK_SOFT(weaponChildren.size() == 1 && weaponChildren[0].GetComponent<NameComponent>().Name == "MuzzleFlash", "grandchild round-tripped");
    }

    std::filesystem::remove(path);
}

TEST_CASE("Instantiate attaches the prefab under the requested live parent") {
    std::string path = TempPrefabPath();

    Scene sourceScene;
    Entity original = sourceScene.CreateEntity("Bullet");
    CHECK(PrefabSerializer::Save(original, path));

    Scene targetScene;
    Entity gun = targetScene.CreateEntity("Gun");
    Entity bulletInstance = PrefabSerializer::Instantiate(targetScene, path, gun);
    CHECK(static_cast<bool>(bulletInstance));
    CHECK_SOFT(bulletInstance.GetComponent<HierarchyComponent>().Parent == gun, "instantiated entity is parented under the requested live entity");

    std::filesystem::remove(path);
}

TEST_CASE("Instantiate fails gracefully for a nonexistent prefab file") {
    Scene scene;
    Entity result = PrefabSerializer::Instantiate(scene, "this_file_does_not_exist.prefab");
    CHECK_SOFT(!result, "Instantiate returns an empty (falsy) Entity when the file can't be opened");
}
