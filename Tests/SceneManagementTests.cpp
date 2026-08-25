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

using namespace Duality;

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
