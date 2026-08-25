// ScriptableObject (ScriptableObjectRegistry/ScriptableObjectLoader) regression coverage.
// GameScripts' own REGISTER_SCRIPTABLE_OBJECT/DLL-export machinery isn't linked into this test
// binary (same reasoning as every other Tests/*.cpp: engine-level logic only) -- a local
// ScriptableObject subclass is registered directly into ScriptableObjectRegistry instead,
// exercising the exact same Create/Destroy/Fields contract GameScripts' own factories fill in.
#include "TestFramework.h"

#include <filesystem>
#include <fstream>

#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

using namespace Duality;

namespace {
    class TestSettingsData : public ScriptableObject {
    public:
        float Health = 100.0f;
        int Coins = 5;

        static std::vector<FieldHandle> Fields() {
            return {
                MakeField("Health", &TestSettingsData::Health),
                MakeField("Coins", &TestSettingsData::Coins),
            };
        }
    };

    void EnsureTestSettingsDataRegistered() {
        if (ScriptableObjectRegistry::Find("TestSettingsData"))
            return;
        ScriptableObjectRegistry::Register(ScriptableObjectFactoryEntry{
            "TestSettingsData",
            []() -> ScriptableObject* { return new TestSettingsData(); },
            [](ScriptableObject* instance) { delete instance; },
            TestSettingsData::Fields(),
        });
    }

    std::string TempAssetPath(const char* filename) {
        return (std::filesystem::temp_directory_path() / filename).string();
    }
}

TEST_CASE("ScriptableObjectRegistry::Register/Find round-trips a type and its Fields") {
    EnsureTestSettingsDataRegistered();

    const ScriptableObjectFactoryEntry* type = ScriptableObjectRegistry::Find("TestSettingsData");
    CHECK(type != nullptr);
    CHECK_SOFT(std::string(type->Name) == "TestSettingsData", "Name round-tripped");
    CHECK_SOFT(type->Fields.size() == 2, "both declared fields are present");
}

TEST_CASE("ScriptableObjectLoader::Create writes defaults and Load reflects live edits") {
    EnsureTestSettingsDataRegistered();
    std::string path = TempAssetPath("duality_engine_test_settings.asset");
    std::filesystem::remove(path);

    ScriptableObjectLoader::Loaded created = ScriptableObjectLoader::Create(path, "TestSettingsData");
    CHECK(created.Instance != nullptr);
    auto* settings = static_cast<TestSettingsData*>(created.Instance);
    CHECK_SOFT(settings->Health == 100.0f, "default Health written on Create");
    CHECK_SOFT(settings->Coins == 5, "default Coins written on Create");

    // Same path -> same cached instance, no reload from disk -- mutate in place and Save,
    // exactly what the Properties panel's auto-save-on-change does.
    settings->Coins = 42;
    CHECK(ScriptableObjectLoader::Save(path, "TestSettingsData", created.Instance));

    ScriptableObjectLoader::Loaded reloaded = ScriptableObjectLoader::Load(path);
    CHECK(reloaded.Instance == created.Instance); // still cache-backed, same pointer
    CHECK_SOFT(static_cast<TestSettingsData*>(reloaded.Instance)->Coins == 42, "edit is visible through a fresh Load() call");

    std::filesystem::remove(path);
}

TEST_CASE("ScriptableObjectLoader round-trips through an actual disk parse") {
    EnsureTestSettingsDataRegistered();
    std::string path = TempAssetPath("duality_engine_test_settings_disk.asset");
    std::filesystem::remove(path);

    ScriptableObjectLoader::Loaded created = ScriptableObjectLoader::Create(path, "TestSettingsData");
    CHECK(created.Instance != nullptr);
    static_cast<TestSettingsData*>(created.Instance)->Health = 33.0f;
    CHECK(ScriptableObjectLoader::Save(path, "TestSettingsData", created.Instance));

    // Drops the in-memory cache (same call ScriptEngine::Shutdown makes before a GameScripts
    // reload/unload) so the next Load() below is forced to re-parse the JSON from disk rather
    // than just handing back the already-mutated in-memory object.
    ScriptableObjectLoader::UnloadAll();

    ScriptableObjectLoader::Loaded reloaded = ScriptableObjectLoader::Load(path);
    CHECK(reloaded.Instance != nullptr);
    CHECK_SOFT(reloaded.Instance != created.Instance, "UnloadAll actually dropped the old cached instance");
    CHECK_SOFT(static_cast<TestSettingsData*>(reloaded.Instance)->Health == 33.0f, "Health survived a real save-to-disk + reparse round trip");

    std::filesystem::remove(path);
}

TEST_CASE("ScriptableObjectLoader::Load fails gracefully for a nonexistent file") {
    ScriptableObjectLoader::Loaded result = ScriptableObjectLoader::Load("this_file_does_not_exist.asset");
    CHECK_SOFT(result.Instance == nullptr, "Load returns a null Instance when the file can't be opened");
}

TEST_CASE("ScriptableObjectLoader::Load fails gracefully for an unregistered class") {
    std::string path = TempAssetPath("duality_engine_test_unknown_class.asset");
    {
        std::ofstream file(path);
        file << R"({"Class": "NoSuchScriptableObjectType", "Fields": {}})";
    }

    ScriptableObjectLoader::Loaded result = ScriptableObjectLoader::Load(path);
    CHECK_SOFT(result.Instance == nullptr, "Load returns a null Instance for an unknown Class");
    CHECK_SOFT(result.ClassName == "NoSuchScriptableObjectType", "ClassName is still reported for diagnostics");

    std::filesystem::remove(path);
}
