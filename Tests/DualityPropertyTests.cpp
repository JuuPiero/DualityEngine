// DUALITY_PROPERTIES (script Inspector fields) + EntityRef regression coverage.
#include "TestFramework.h"

#include <filesystem>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

    class PropsBehaviour : public Behaviour {
    public:
        float Speed = 5.0f;
        bool Flag = false;
        EntityRef Target;

        DUALITY_PROPERTIES(PropsBehaviour, Speed, Flag, Target)

        void OnCreate() override {
            s_CreatedSpeed = Speed;
            s_CreatedFlag = Flag;
        }

        static float s_CreatedSpeed;
        static bool s_CreatedFlag;
    };
    float PropsBehaviour::s_CreatedSpeed = 0.0f;
    bool PropsBehaviour::s_CreatedFlag = false;

    void EnsureRegistered() {
        static bool registered = false;
        if (registered)
            return;
        ScriptRegistry::Register(ScriptFactoryEntry{
            "PropsBehaviour",
            []() -> Behaviour* { return new PropsBehaviour(); },
            [](Behaviour* instance) { delete instance; },
            PropsBehaviour::Fields(),
        });
        registered = true;
    }

    std::string TempScenePath() {
        return (std::filesystem::temp_directory_path() / "duality_engine_test_property_scene.scene").string();
    }

}

TEST_CASE("DUALITY_PROPERTIES generates Fields() in declaration order") {
    auto fields = PropsBehaviour::Fields();
    CHECK(fields.size() == 3);
    CHECK_SOFT(fields[0].Name == "Speed", "first field name matches declaration order");
    CHECK_SOFT(fields[1].Name == "Flag", "second field name matches declaration order");
    CHECK_SOFT(fields[2].Name == "Target", "third field name matches declaration order");
}

TEST_CASE("ScriptRegistry::GetFields returns {} for a script with no DUALITY_PROPERTIES") {
    EnsureRegistered(); // registers PropsBehaviour, not the class below
    ScriptRegistry::Register(ScriptFactoryEntry{
        "NoPropsBehaviour",
        []() -> Behaviour* { return nullptr; },
        [](Behaviour*) {},
    });
    CHECK_SOFT(ScriptRegistry::GetFields("NoPropsBehaviour").empty(), "class without DUALITY_PROPERTIES has zero reflected fields");
    CHECK_SOFT(ScriptRegistry::GetFields("PropsBehaviour").size() == 3, "class with DUALITY_PROPERTIES still resolves correctly");
    CHECK_SOFT(ScriptRegistry::GetFields("NoSuchClass").empty(), "an unregistered class name returns {} rather than crashing");
}

TEST_CASE("BehaviourComponent::PropertyOverrides apply onto the instance before OnCreate") {
    EnsureRegistered();
    Scene scene;
    Entity e = scene.CreateEntity("Scripted");
    auto& bc = e.AddComponent<BehaviourComponent>();
    bc.ClassName = "PropsBehaviour";
    bc.PropertyOverrides["Speed"] = FieldValue(9.5f);
    bc.PropertyOverrides["Flag"] = FieldValue(true);

    scene.OnRuntimeStart();

    auto* instance = static_cast<PropsBehaviour*>(bc.Instance);
    CHECK_SOFT(instance->Speed == 9.5f, "float override applied before OnCreate saw it");
    CHECK_SOFT(instance->Flag == true, "bool override applied too");
    CHECK_SOFT(PropsBehaviour::s_CreatedSpeed == 9.5f, "OnCreate itself observed the overridden value, not the field's compiled-in default");
    CHECK_SOFT(PropsBehaviour::s_CreatedFlag == true, "same for the bool field");
}

TEST_CASE("An entity with no PropertyOverrides keeps the script's compiled-in defaults") {
    EnsureRegistered();
    Scene scene;
    Entity e = scene.CreateEntity("Scripted");
    e.AddComponent<BehaviourComponent>().ClassName = "PropsBehaviour";

    scene.OnRuntimeStart();

    auto* instance = static_cast<PropsBehaviour*>(e.GetComponent<BehaviourComponent>().Instance);
    CHECK_SOFT(instance->Speed == 5.0f, "no override present -- field keeps its C++ default");
    CHECK_SOFT(instance->Flag == false, "same for the bool field");
}

TEST_CASE("PropertyOverrides round-trip through SceneSerializer; EntityRef always reloads unset") {
    EnsureRegistered();
    std::string path = TempScenePath();

    Scene sourceScene;
    Entity target = sourceScene.CreateEntity("Target");
    Entity e = sourceScene.CreateEntity("Scripted");
    auto& bc = e.AddComponent<BehaviourComponent>();
    bc.ClassName = "PropsBehaviour";
    bc.PropertyOverrides["Speed"] = FieldValue(3.25f);
    bc.PropertyOverrides["Flag"] = FieldValue(true);
    bc.PropertyOverrides["Target"] = FieldValue(EntityRef{ static_cast<uint32_t>(target.Handle()) });

    CHECK(SceneSerializer(sourceScene).Serialize(path));

    Scene loadedScene;
    CHECK(SceneSerializer(loadedScene).Deserialize(path));

    Entity loaded = loadedScene.FindEntityInScreen(Screen::Top, "Scripted");
    CHECK(loaded);
    auto& loadedBc = loaded.GetComponent<BehaviourComponent>();
    CHECK_SOFT(loadedBc.ClassName == "PropsBehaviour", "ClassName itself round-trips (already worked before this feature)");

    auto speedIt = loadedBc.PropertyOverrides.find("Speed");
    CHECK(speedIt != loadedBc.PropertyOverrides.end());
    CHECK_SOFT(std::get<float>(speedIt->second) == 3.25f, "float override survives a save/load round trip");

    auto flagIt = loadedBc.PropertyOverrides.find("Flag");
    CHECK(flagIt != loadedBc.PropertyOverrides.end());
    CHECK_SOFT(std::get<bool>(flagIt->second) == true, "bool override survives a save/load round trip");

    auto targetIt = loadedBc.PropertyOverrides.find("Target");
    CHECK(targetIt != loadedBc.PropertyOverrides.end());
    CHECK_SOFT(std::get<EntityRef>(targetIt->second).Handle == EntityRef::Invalid,
        "EntityRef never round-trips -- a raw handle isn't stable across reload, so it always reloads as unset rather than resolving to the wrong entity");

    std::filesystem::remove(path);
}
