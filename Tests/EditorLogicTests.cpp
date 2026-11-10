// Editor logic that has no UI of its own: Undo history (SceneHistory + stable entity ids),
// clipboard Copy/Paste serialization, EntityRef remapping in prefabs, and the Inspector's
// multi-object editing rules. The ImGui side is covered by `DualityEditor --editor-test`.
#include "TestFramework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "DualityEditor/EntityIdRegistry.h"
#include "DualityEditor/MultiEdit.h"
#include "DualityEditor/SceneHistory.h"
#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;

namespace {
    Entity FindNamed(Scene& scene, const std::string& name, int occurrence = 1) {
        for (Entity entity : scene.GetHierarchyTraversalOrder()) {
            if (entity.GetComponent<NameComponent>().Name == name && --occurrence == 0)
                return entity;
        }
        return {};
    }

    Entity Resolve(Scene& scene, const EntityRef& reference) {
        const entt::entity handle = static_cast<entt::entity>(reference.Handle);
        if (reference.Handle == EntityRef::Invalid || !scene.Registry().valid(handle))
            return {};
        return Entity(handle, &scene);
    }

    EntityRef RefTo(Entity entity) { return EntityRef{ static_cast<uint32_t>(entity.Handle()) }; }
}

TEST_CASE("SceneHistory: undo/redo restores scene, labels and selection by stable id") {
    Scene scene;
    EntityIdRegistry ids;
    Entity a = scene.CreateEntity("A");
    SceneHistory history;
    history.Reset(scene, ids, { { ids.Get(a) }, ids.Get(a) });
    const uint64_t idA = ids.Get(a);

    // Change 1: move A.
    a.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    CHECK(history.Capture(scene, ids, "Move", { { idA }, idA }, { { idA }, idA }));
    // Change 2: create B (selected afterwards).
    Entity b = scene.CreateEntity("B");
    const uint64_t idB = ids.Get(b);
    CHECK(history.Capture(scene, ids, "Create Empty", { { idA }, idA }, { { idB }, idB }));
    CHECK_SOFT(history.UndoLabel() == "Create Empty", "undo label is the last change");
    CHECK_SOFT(!history.CanRedo(), "nothing to redo yet");

    HistorySelection selection;
    CHECK(history.Undo(scene, ids, &selection));
    CHECK_SOFT(!FindNamed(scene, "B"), "undo removed B");
    CHECK_SOFT(selection.Primary == idA, "undo restores the selection before the change");
    CHECK_SOFT(ids.Find(scene, idA) == FindNamed(scene, "A"), "A keeps its stable id across the rebuild");
    CHECK_SOFT(history.UndoLabel() == "Move" && history.RedoLabel() == "Create Empty", "labels follow the cursor");

    CHECK(history.Redo(scene, ids, &selection));
    CHECK_SOFT(static_cast<bool>(FindNamed(scene, "B")), "redo re-creates B");
    CHECK_SOFT(selection.Primary == idB && ids.Find(scene, idB) == FindNamed(scene, "B"), "redo selects B by its old id");

    CHECK(history.Undo(scene, ids));
    CHECK(history.Undo(scene, ids));
    const glm::vec3 position = FindNamed(scene, "A").GetComponent<TransformComponent>().Translation;
    CHECK_SOFT(position == glm::vec3(0.0f), "second undo reverts the move");
    CHECK_SOFT(!history.CanUndo(), "back at the initial state");
    CHECK_SOFT(history.IsAtSavedState(), "initial state is the saved state");
}

TEST_CASE("SceneHistory: identical snapshot is not a new step; new change drops redo branch") {
    Scene scene;
    EntityIdRegistry ids;
    scene.CreateEntity("A");
    SceneHistory history;
    history.Reset(scene, ids);
    CHECK_SOFT(!history.Capture(scene, ids, "Nothing"), "unchanged scene does not add a step");
    FindNamed(scene, "A").GetComponent<NameComponent>().Name = "A2";
    CHECK(history.Capture(scene, ids, "Rename"));
    CHECK(history.Undo(scene, ids));
    FindNamed(scene, "A").GetComponent<TagComponent>().Tag = "Player";
    CHECK(history.Capture(scene, ids, "Change Tag"));
    CHECK_SOFT(!history.CanRedo(), "a new change discards the redo branch");
    CHECK_SOFT(history.StateCount() == 2, "initial + Change Tag");
    history.MarkSaved(scene, ids);
    CHECK_SOFT(history.IsAtSavedState(), "MarkSaved marks the current state");
}

TEST_CASE("SceneHistory: bounded to MaxStates") {
    Scene scene;
    EntityIdRegistry ids;
    Entity a = scene.CreateEntity("A");
    SceneHistory history;
    history.Reset(scene, ids);
    for (size_t i = 0; i < SceneHistory::MaxStates + 20; ++i) {
        a = FindNamed(scene, "A");
        a.GetComponent<TransformComponent>().Translation.x = static_cast<float>(i + 1);
        history.Capture(scene, ids, "Move");
    }
    CHECK_SOFT(history.StateCount() == SceneHistory::MaxStates, "history keeps at most MaxStates");
    CHECK_SOFT(history.CanUndo(), "still undoable");
}

TEST_CASE("EntityIdRegistry: ids survive a JSON rebuild of the scene") {
    Scene scene;
    EntityIdRegistry ids;
    Entity root = scene.CreateEntity("Root");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, root);
    const uint64_t rootId = ids.Get(root), childId = ids.Get(child);
    const std::vector<uint64_t> order = ids.CaptureOrder(scene);
    const nlohmann::json snapshot = SceneSerializer(scene).SerializeToJson();
    scene.Clear();
    CHECK(SceneSerializer(scene).DeserializeFromJson(snapshot));
    ids.ApplyOrder(scene, order);
    CHECK_SOFT(ids.Find(scene, rootId) == FindNamed(scene, "Root"), "root id re-attached");
    CHECK_SOFT(ids.Find(scene, childId) == FindNamed(scene, "Child"), "child id re-attached");
    Entity fresh = scene.CreateEntity("Fresh");
    CHECK_SOFT(ids.Get(fresh) != rootId && ids.Get(fresh) != childId, "new entities get new ids");
}

TEST_CASE("Clipboard: copy/paste keeps internal refs on the copies and external refs on the originals") {
    Scene scene;
    Entity target = scene.CreateEntity("Target");
    Entity root = scene.CreateEntity("Root");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, root);
    Entity group = scene.CreateEntity("Group");
    group.GetComponent<TransformComponent>().Translation = { 10.0f, 0.0f, 0.0f };
    root.GetComponent<TransformComponent>().Translation = { 3.0f, 4.0f, 0.0f };
    root.AddComponent<FollowTargetComponent>().Target = RefTo(target);   // external
    child.AddComponent<FollowTargetComponent>().Target = RefTo(root);    // internal

    const nlohmann::json clipboard = PrefabSerializer::CopyToJson(scene, { root, child });
    CHECK_SOFT(clipboard["Entities"].size() == 2, "child folded into its copied parent");
    const std::vector<Entity> pasted = PrefabSerializer::PasteFromJson(scene, clipboard, group);
    CHECK(pasted.size() == 1);
    Entity copy = pasted.front();
    CHECK(copy != root);
    CHECK_SOFT(copy.GetComponent<HierarchyComponent>().Parent == group, "pasted under the requested parent");
    const TransformComponent world = scene.GetWorldTransform(copy);
    CHECK_SOFT(std::abs(world.Translation.x - 3.0f) < 1e-4f && std::abs(world.Translation.y - 4.0f) < 1e-4f, "world position preserved under the new parent");
    CHECK_SOFT(Resolve(scene, copy.GetComponent<FollowTargetComponent>().Target) == target, "external reference kept");
    CHECK(copy.GetComponent<HierarchyComponent>().Children.size() == 1);
    Entity copiedChild = copy.GetComponent<HierarchyComponent>().Children.front();
    CHECK_SOFT(Resolve(scene, copiedChild.GetComponent<FollowTargetComponent>().Target) == copy, "internal reference remapped to the copy");
    CHECK_SOFT(Resolve(scene, child.GetComponent<FollowTargetComponent>().Target) == root, "original untouched");
}

TEST_CASE("Clipboard: external handles are remapped after the scene was rebuilt (Undo)") {
    Scene scene;
    Entity target = scene.CreateEntity("Target");
    Entity source = scene.CreateEntity("Source");
    source.AddComponent<FollowTargetComponent>().Target = RefTo(target);
    const nlohmann::json clipboard = PrefabSerializer::CopyToJson(scene, { source });
    const uint32_t oldTargetHandle = static_cast<uint32_t>(target.Handle());

    // Rebuild the scene (what Undo/Redo do): every handle may change.
    const nlohmann::json snapshot = SceneSerializer(scene).SerializeToJson();
    scene.Clear();
    scene.CreateEntity("Padding"); // shift handle allocation
    CHECK(SceneSerializer(scene).DeserializeFromJson(snapshot));
    Entity newTarget = FindNamed(scene, "Target");
    std::unordered_map<uint32_t, uint32_t> remap{ { oldTargetHandle, static_cast<uint32_t>(newTarget.Handle()) } };
    const std::vector<Entity> pasted = PrefabSerializer::PasteFromJson(scene, clipboard, {}, &remap);
    CHECK(pasted.size() == 1);
    CHECK_SOFT(Resolve(scene, pasted.front().GetComponent<FollowTargetComponent>().Target) == newTarget, "reference follows the remap");

    // A target that no longer exists pastes as "none" instead of aliasing another entity.
    std::unordered_map<uint32_t, uint32_t> gone{ { oldTargetHandle, EntityRef::Invalid } };
    const std::vector<Entity> pastedAgain = PrefabSerializer::PasteFromJson(scene, clipboard, {}, &gone);
    CHECK(pastedAgain.size() == 1);
    CHECK_SOFT(pastedAgain.front().GetComponent<FollowTargetComponent>().Target.Handle == EntityRef::Invalid, "missing target becomes none");
}

TEST_CASE("Prefab: EntityRefs inside the prefab survive save/instantiate; outside refs become none") {
    const std::string path = (std::filesystem::temp_directory_path() / "duality_editor_ref_test.prefab").string();
    Scene scene;
    Entity outside = scene.CreateEntity("Outside");
    Entity root = scene.CreateEntity("Root");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, root);
    root.AddComponent<FollowTargetComponent>().Target = RefTo(child);
    child.AddComponent<FollowTargetComponent>().Target = RefTo(outside);
    CHECK(PrefabSerializer::Save(root, path));

    Scene other;
    other.CreateEntity("Filler1");
    other.CreateEntity("Filler2");
    Entity instance = PrefabSerializer::Instantiate(other, path);
    CHECK(static_cast<bool>(instance));
    Entity instanceChild = instance.GetComponent<HierarchyComponent>().Children.front();
    CHECK_SOFT(Resolve(other, instance.GetComponent<FollowTargetComponent>().Target) == instanceChild, "internal ref points at the instance's own child");
    CHECK_SOFT(instanceChild.GetComponent<FollowTargetComponent>().Target.Handle == EntityRef::Invalid, "ref to an entity outside the prefab is none");

    // Two instances must not share targets.
    Entity second = PrefabSerializer::Instantiate(other, path);
    Entity secondChild = second.GetComponent<HierarchyComponent>().Children.front();
    CHECK_SOFT(Resolve(other, second.GetComponent<FollowTargetComponent>().Target) == secondChild, "second instance references its own child");
    std::remove(path.c_str());
}

TEST_CASE("Prefab: legacy files with raw {\"$entity\": handle} load as none instead of aliasing") {
    const std::string path = (std::filesystem::temp_directory_path() / "duality_editor_legacy_ref.prefab").string();
    {
        std::ofstream file(path);
        file << R"({"WorldUnitsVersion":2,"CanvasUIVersion":1,"Entities":[{"Name":{"Name":"Old"},"Follow Target":{"Enabled":true,"Target":{"$entity":0}},"Parent":-1}]})";
    }
    Scene scene;
    scene.CreateEntity("HandleZero");
    Entity instance = PrefabSerializer::Instantiate(scene, path);
    CHECK(static_cast<bool>(instance));
    CHECK_SOFT(instance.GetComponent<FollowTargetComponent>().Target.Handle == EntityRef::Invalid, "stale raw handle dropped");
    std::remove(path.c_str());
}

TEST_CASE("MultiEdit: mixed mask per vector element and per-element propagation") {
    Scene scene;
    Entity a = scene.CreateEntity("A");
    Entity b = scene.CreateEntity("B");
    a.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    b.GetComponent<TransformComponent>().Translation = { 1.0f, 5.0f, 3.0f };
    ComponentTypeInfo* transform = TypeRegistry::Find("Transform");
    CHECK(transform != nullptr);
    const FieldHandle* translation = nullptr;
    for (const FieldHandle& field : transform->Fields)
        if (field.Name == "Translation")
            translation = &field;
    CHECK(translation != nullptr);
    const std::vector<Entity> targets = { a, b };
    CHECK_SOFT(MultiEdit::MixedMask(*translation, *transform, targets) == 0b010u, "only Y differs");

    // Edit X on the primary: only X is copied to B (B keeps its own Y).
    const nlohmann::json before = FieldValueToJson(translation->Get(transform->GetPtr(a)));
    a.GetComponent<TransformComponent>().Translation.x = 7.0f;
    const nlohmann::json after = FieldValueToJson(translation->Get(transform->GetPtr(a)));
    MultiEdit::Propagate(*translation, *transform, targets, before, after);
    CHECK_SOFT(b.GetComponent<TransformComponent>().Translation == glm::vec3(7.0f, 5.0f, 3.0f), "X propagated, Y kept");

    // Non-vector values are copied whole.
    ComponentTypeInfo* name = TypeRegistry::Find("Name");
    const FieldHandle& nameField = name->Fields.front();
    CHECK_SOFT(MultiEdit::MixedMask(nameField, *name, targets) == 1u, "different names are mixed");
    const nlohmann::json nameBefore = FieldValueToJson(nameField.Get(name->GetPtr(a)));
    a.GetComponent<NameComponent>().Name = "Same";
    MultiEdit::Propagate(nameField, *name, targets, nameBefore, FieldValueToJson(nameField.Get(name->GetPtr(a))));
    CHECK_SOFT(b.GetComponent<NameComponent>().Name == "Same", "name propagated");
    CHECK_SOFT(MultiEdit::MixedMask(nameField, *name, targets) == 0u, "no longer mixed");
}
