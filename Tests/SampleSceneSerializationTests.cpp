// Loads every scene and prefab of SampleProject through the real serializers and checks that
// saving is stable (load -> save -> load -> save gives the same JSON). With the environment
// variable DUALITY_DUMP_SERIALIZED=<dir> set, the first save of each file is also written to
// <dir>/<file>.json, so two builds of the engine can be diffed for format changes.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "TestFramework.h"

using namespace Duality;
using json = nlohmann::json;

namespace {
    std::filesystem::path SampleAssets() {
        return std::filesystem::path(DUALITY_REPO_ROOT) / "SampleProject" / "Assets";
    }

    bool ReadJson(const std::filesystem::path& path, json& out) {
        std::ifstream file(path);
        if (!file.is_open())
            return false;
        out = json::parse(file, nullptr, false);
        return !out.is_discarded();
    }

    json SaveScene(const json& source) {
        Scene scene;
        SceneSerializer(scene).DeserializeFromJson(source);
        return SceneSerializer(scene).SerializeToJson();
    }

    json SavePrefabInstance(const std::filesystem::path& prefabPath) {
        Scene scene;
        PrefabSerializer::Instantiate(scene, prefabPath.string(), Entity{});
        return SceneSerializer(scene).SerializeToJson();
    }

    void DumpAs(const std::string& name, const json& saved) {
        const char* dumpDir = std::getenv("DUALITY_DUMP_SERIALIZED");
        if (!dumpDir || !dumpDir[0])
            return;
        std::filesystem::create_directories(dumpDir);
        std::ofstream(std::filesystem::path(dumpDir) / (name + ".json")) << saved.dump(2);
    }

    void Dump(const std::filesystem::path& assetPath, const json& saved) {
        std::string name = std::filesystem::relative(assetPath, SampleAssets()).generic_string();
        for (char& c : name)
            if (c == '/')
                c = '_';
        DumpAs(name, saved);
    }
}

TEST_CASE("An entity with every registered component saves and reloads stably") {
    Scene scene;
    Entity entity = scene.CreateEntity("Everything");
    int registered = 0;
    for (ComponentTypeInfo& type : TypeRegistry::All()) {
        type.AddDefault(entity);
        registered++;
    }
    const json first = SceneSerializer(scene).SerializeToJson();
    DumpAs("AllComponents", first);
    // Registry metadata in registration order (Inspector order, field presentation).
    json registry = json::array();
    for (ComponentTypeInfo& type : TypeRegistry::All()) {
        json fields = json::array();
        for (const FieldHandle& field : type.Fields)
            fields.push_back({ field.Name, field.HasRange, field.RangeMin, field.RangeMax, field.Tooltip, field.Header });
        registry.push_back({ { "Name", type.DisplayName }, { "Mandatory", type.Mandatory }, { "Category", type.Category }, { "Header", type.HeaderComponent }, { "Fields", fields } });
    }
    DumpAs("Registry", registry);
    const json second = SaveScene(first);
    CHECK_SOFT(first == second, "every component round-trips");
    CHECK_SOFT(registered > 80, "registry is populated");
    for (ComponentTypeInfo& type : TypeRegistry::All())
        CHECK_SOFT(first["Entities"][0].contains(type.DisplayName), ("serialized " + type.DisplayName).c_str());
}

TEST_CASE("Every SampleProject scene and prefab saves stably (load/save/load/save)") {
    CHECK(std::filesystem::exists(SampleAssets()));
    int files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(SampleAssets())) {
        if (!entry.is_regular_file())
            continue;
        const std::string extension = entry.path().extension().string();
        if (extension != ".scene" && extension != ".prefab")
            continue;
        files++;
        json first;
        if (extension == ".scene") {
            json source;
            CHECK_SOFT(ReadJson(entry.path(), source), entry.path().string().c_str());
            first = SaveScene(source);
        } else {
            first = SavePrefabInstance(entry.path());
        }
        Dump(entry.path(), first);
        const json second = SaveScene(first);
        CHECK_SOFT(first == second, ("stable save of " + entry.path().filename().string()).c_str());
    }
    CHECK_SOFT(files >= 7, "found the sample scenes/prefabs");
}
