// TypeScript scripting (QuickJS) bridge tests. The fixture scripts live in
// Tests/TypeScriptFixture/Assets/Scripts/Probes.ts and are bundled at build time by
// Tools/TypeScript/build-scripts.mjs (Tests/CMakeLists.txt) -- DUALITY_TS_FIXTURE is the bundle.
// Without node at configure time the fixture can't be built and these tests are skipped.
#include "TestFramework.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include <box2d/box2d.h>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/JS/JsScripting.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

using namespace Duality;

namespace {

#ifdef DUALITY_TS_FIXTURE
    constexpr const char* kFixture = DUALITY_TS_FIXTURE;
#else
    constexpr const char* kFixture = "";
#endif

    bool LoadFixture() {
        if (!*kFixture || !std::filesystem::exists(kFixture)) {
            std::printf("         (skipped: TypeScript fixture bundle not built -- node not found at configure time?)\n");
            return false;
        }
        return JsScripting::LoadBundle(kFixture);
    }

    std::string JsLog() {
        std::string result;
        JsScripting::Eval("JSON.stringify(globalThis.__log || [])", &result);
        return result;
    }
    bool LogHas(const std::string& entry) {
        return JsLog().find("\"" + entry + "\"") != std::string::npos;
    }
    void Step(Scene& scene, int frames) {
        for (int i = 0; i < frames; ++i)
            scene.OnRuntimeUpdate(1.0f / 60.0f);
    }
    ScriptInstance& AddScript(Entity e, const std::string& className) {
        auto& bc = e.HasComponent<BehaviourComponent>() ? e.GetComponent<BehaviourComponent>() : e.AddComponent<BehaviourComponent>();
        bc.Scripts.push_back(ScriptInstance{ className });
        return bc.Scripts.back();
    }
    const FieldHandle* FindField(const std::string& className, const std::string& name) {
        for (const FieldHandle& field : ScriptRegistry::GetFields(className))
            if (field.Name == name)
                return &field;
        return nullptr;
    }

    // A C++ script the TS CrossProbe talks to (and that talks back to TS).
    struct NativeCounter : public Behaviour {
        int Count = 7;
        float SeenSpeed = -1.0f;
        bool Pinged = false;
        int Frames = 0;
        void OnUpdate(float) override {
            if (++Frames != 2)
                return; // frame 2: every TS start() has run
            GetScriptField("FieldProbe", "speed", SeenSpeed);
            Pinged = SendScriptMessage("CrossProbe", "ping");
        }
    };
    NativeCounter* g_LastCounter = nullptr;

    void RegisterNativeCounter() {
        ScriptRegistry::Register(ScriptFactoryEntry{
            "NativeCounter",
            []() -> Behaviour* { auto* c = new NativeCounter(); g_LastCounter = c; return c; },
            [](Behaviour* b) { delete b; },
            { MakeField("Count", &NativeCounter::Count) },
        });
    }

}

TEST_CASE("TS bundle registers exported MonoBehaviours next to C++ scripts") {
    if (!LoadFixture()) return;
    RegisterNativeCounter();
    CHECK(JsScripting::IsLoaded());
    CHECK_SOFT(ScriptRegistry::IsRegistered("LifecycleProbe"), "LifecycleProbe registered");
    CHECK_SOFT(ScriptRegistry::GetLanguage("LifecycleProbe") == ScriptLanguage::TypeScript, "tagged as TypeScript");
    CHECK_SOFT(ScriptRegistry::GetLanguage("NativeCounter") == ScriptLanguage::Cpp, "C++ class still tagged C++");
    CHECK_SOFT(JsScripting::GetClassSourceFile("FieldProbe") == "Assets/Scripts/Probes.ts", "source file recorded");

    // speed lives flag label offset tint mode ranged target prefab -- `_hidden` is private.
    CHECK_SOFT(ScriptRegistry::GetFields("FieldProbe").size() == 10, "10 inspector fields on FieldProbe");
    const FieldHandle* ranged = FindField("FieldProbe", "ranged");
    CHECK_SOFT(ranged && ranged->HasRange && ranged->RangeMax == 10.0f && ranged->Tooltip == "a slider", "@field range/tooltip metadata reaches the Inspector");
    CHECK_SOFT(FindField("FieldProbe", "_hidden") == nullptr, "underscore fields are not serialized");

    // Scratch instance (what the Properties panel creates every frame): defaults, no JS object.
    Behaviour* scratch = nullptr;
    void (*destroy)(Behaviour*) = nullptr;
    CHECK(ScriptRegistry::TryCreate("FieldProbe", &scratch, &destroy));
    const FieldHandle* speed = FindField("FieldProbe", "speed");
    const FieldHandle* lives = FindField("FieldProbe", "lives");
    const FieldHandle* mode = FindField("FieldProbe", "mode");
    CHECK(speed && lives && mode);
    CHECK_SOFT(std::get<float>(speed->Get(scratch)) == 2.5f, "float default from the field initializer");
    CHECK_SOFT(std::get<int>(lives->Get(scratch)) == 3, "@field('int') is an int field");
    CHECK_SOFT(std::get<EnumFieldValue>(mode->Get(scratch)).Value == 1 && std::get<EnumFieldValue>(mode->Get(scratch)).Options.size() == 3, "options -> enum dropdown");
    destroy(scratch);
    CHECK_SOFT(JsScripting::GetLiveInstanceCount() == 0, "scratch instances never create JS objects");

    // C++ DLL reload clears only C++ classes; TS unload clears only TS classes.
    ScriptRegistry::ClearLanguage(ScriptLanguage::Cpp);
    CHECK_SOFT(ScriptRegistry::IsRegistered("FieldProbe"), "TS classes survive a C++ reload");
    RegisterNativeCounter();
    JsScripting::Unload();
    CHECK_SOFT(!ScriptRegistry::IsRegistered("FieldProbe") && ScriptRegistry::IsRegistered("NativeCounter"), "Unload drops TS classes only");
}

TEST_CASE("TS lifecycle runs in the same order as C++ Behaviours") {
    if (!LoadFixture()) return;
    Scene scene;
    Entity e = scene.CreateEntity("Probe");
    AddScript(e, "LifecycleProbe");
    scene.OnRuntimeStart();
    Step(scene, 3);
    e.GetComponent<ActiveComponent>().Active = false;
    Step(scene, 1);
    e.GetComponent<ActiveComponent>().Active = true;
    Step(scene, 1);
    scene.OnRuntimeStop();
    const std::string expected = "[\"awake:Probe\",\"onEnable\",\"start\",\"update\",\"lateUpdate\",\"fixedUpdate\",\"update\",\"lateUpdate\","
                                 "\"onDisable\",\"onEnable\",\"onDisable\",\"onDestroy\"]";
    const std::string actual = JsLog();
    if (actual != expected)
        std::printf("         got      %s\n         expected %s\n", actual.c_str(), expected.c_str());
    CHECK_SOFT(actual == expected, "awake -> onEnable -> start -> update -> lateUpdate, fixedUpdate from frame 2, enable/disable edges, onDestroy at Stop");
    CHECK_SOFT(JsScripting::GetLiveInstanceCount() == 0, "Stop destroyed the JS instance");
    JsScripting::Unload();
}

TEST_CASE("TS inspector fields round-trip through the scene JSON and reach the script") {
    if (!LoadFixture()) return;
    nlohmann::json saved;
    {
        Scene scene;
        Entity other = scene.CreateEntity("Other");
        Entity e = scene.CreateEntity("Holder");
        AddScript(e, "FieldProbe");
        ScriptInstance& slot = e.GetComponent<BehaviourComponent>().Scripts.back();
        slot.PropertyOverrides["speed"] = 9.0f;
        slot.PropertyOverrides["lives"] = 5;
        slot.PropertyOverrides["label"] = std::string("edited");
        slot.PropertyOverrides["offset"] = glm::vec2(3.0f, 4.0f);
        EnumFieldValue mode; mode.Value = 2; mode.Options = { "Idle", "Run", "Jump" };
        slot.PropertyOverrides["mode"] = mode;
        slot.PropertyOverrides["target"] = EntityRef{ static_cast<uint32_t>(other.Handle()) };
        slot.PropertyOverrides["prefab"] = AssetRef{ "0123456789abcdef0123456789abcdef" };
        saved = SceneSerializer(scene).SerializeToJson();
    }
    const std::string text = saved.dump();
    CHECK_SOFT(text.find("\"ClassName\":\"FieldProbe\"") != std::string::npos, "same Scripts/ClassName JSON as C++ scripts");

    Scene loaded;
    CHECK(SceneSerializer(loaded).DeserializeFromJson(saved));
    Entity holder;
    for (auto handle : loaded.Registry().view<BehaviourComponent>())
        holder = Entity(handle, &loaded);
    CHECK(holder);
    auto& overrides = holder.GetComponent<BehaviourComponent>().Scripts.back().PropertyOverrides;
    CHECK_SOFT(overrides.count("speed") && std::get<float>(overrides["speed"]) == 9.0f, "float override survives");
    CHECK_SOFT(overrides.count("mode") && std::get<EnumFieldValue>(overrides["mode"]).Value == 2, "enum override survives");

    loaded.OnRuntimeStart();
    Step(loaded, 1);
    const bool ok = LogHas("fields:9|5|true|edited|3,4|1|2|5|Other|0123456789abcdef0123456789abcdef|42");
    if (!ok)
        std::printf("         log: %s\n", JsLog().c_str());
    CHECK_SOFT(ok, "overrides (incl. entity + asset refs) were applied before start()");

    // Live instance: the Inspector reads/writes through the same FieldHandles.
    ScriptInstance& live = holder.GetComponent<BehaviourComponent>().Scripts.back();
    const FieldHandle* speed = FindField("FieldProbe", "speed");
    CHECK(speed && live.Instance);
    CHECK_SOFT(std::get<float>(speed->Get(live.Instance)) == 9.0f, "live Get reads the JS property");
    speed->Set(live.Instance, 12.5f);
    std::string jsValue;
    JsScripting::Eval("String(1)", &jsValue);
    CHECK_SOFT(std::get<float>(speed->Get(live.Instance)) == 12.5f, "live Set writes the JS property");
    loaded.OnRuntimeStop();
    JsScripting::Unload();
}

TEST_CASE("TS 2D/3D collision and trigger callbacks") {
    if (!LoadFixture()) return;
    Scene scene;
    Entity a = scene.CreateEntity("BallA");
    a.AddComponent<Rigidbody2DComponent>();
    a.AddComponent<CircleCollider2DComponent>().Radius = 1.0f;
    AddScript(a, "CollisionProbe");
    Entity b = scene.CreateEntity("BallB");
    b.GetComponent<TransformComponent>().Translation = { 0.5f, 0.0f, 0.0f };
    b.AddComponent<Rigidbody2DComponent>();
    b.AddComponent<CircleCollider2DComponent>().Radius = 1.0f;
    AddScript(b, "CollisionProbe");

    Entity zone = scene.CreateEntity("Zone");
    zone.GetComponent<TransformComponent>().Translation = { 50.0f, 0.0f, 0.0f };
    auto& zoneBox = zone.AddComponent<BoxCollider2DComponent>();
    zoneBox.Size = { 2.0f, 2.0f };
    zoneBox.IsTrigger = true;
    AddScript(zone, "CollisionProbe");
    Entity visitor = scene.CreateEntity("Visitor");
    visitor.GetComponent<TransformComponent>().Translation = { 50.0f, 0.0f, 0.0f };
    visitor.AddComponent<Rigidbody2DComponent>().GravityScale = 0.0f;
    visitor.AddComponent<CircleCollider2DComponent>().Radius = 0.5f;

    Entity ground = scene.CreateEntity("Ground3D");
    ground.GetComponent<TransformComponent>().Translation = { 0.0f, 50.0f, 100.0f };
    ground.AddComponent<Rigidbody3DComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxCollider3DComponent>().Size = { 100.0f, 10.0f, 100.0f };
    Entity ball = scene.CreateEntity("Ball3D");
    ball.GetComponent<TransformComponent>().Translation = { 0.0f, 30.0f, 100.0f };
    ball.AddComponent<Rigidbody3DComponent>();
    ball.AddComponent<SphereCollider3DComponent>().Radius = 15.0f;
    AddScript(ball, "Collision3DProbe");

    scene.OnRuntimeStart();
    Step(scene, 3);
    CHECK_SOFT(LogHas("collisionEnter2D:BallA<-BallB") && LogHas("collisionEnter2D:BallB<-BallA"), "2D collision enter on both sides with Collision2D");
    CHECK_SOFT(LogHas("triggerEnter2D:Zone<-Visitor"), "2D trigger enter with the other Collider2D");
    CHECK_SOFT(LogHas("collisionEnter3D:Ball3D<-Ground3D"), "3D collision enter");
    static_cast<b2Body*>(b.GetComponent<Rigidbody2DComponent>().RuntimeBody)->SetTransform(b2Vec2(1000.0f, 0.0f), 0.0f);
    Step(scene, 1);
    CHECK_SOFT(LogHas("collisionExit2D:BallA<-BallB"), "2D collision exit");
    scene.OnRuntimeStop();
    JsScripting::Unload();
}

TEST_CASE("getComponent across C++ and TypeScript scripts, both directions") {
    if (!LoadFixture()) return;
    RegisterNativeCounter();
    Scene scene;
    Entity e = scene.CreateEntity("Mixed");
    e.AddComponent<Rigidbody2DComponent>().GravityScale = 0.0f;
    AddScript(e, "NativeCounter");
    AddScript(e, "FieldProbe");
    AddScript(e, "CrossProbe");
    scene.OnRuntimeStart();
    Step(scene, 2);
    CHECK_SOFT(LogHas("native:7"), "TS read a C++ DUALITY_PROPERTY field via getComponent('NativeCounter')");
    CHECK_SOFT(LogHas("ts:2.5"), "TS getComponent(FieldProbe) returned the other TS script");
    CHECK_SOFT(LogHas("rb:yes"), "TS getComponent(Rigidbody2D) built-in wrapper");
    CHECK(g_LastCounter);
    CHECK_SOFT(g_LastCounter->Count == 41, "TS wrote the C++ field");
    CHECK_SOFT(g_LastCounter->SeenSpeed == 2.5f, "C++ read a TS @field through GetScriptField");
    CHECK_SOFT(g_LastCounter->Pinged && LogHas("ping"), "C++ called a TS method through SendScriptMessage");
    scene.OnRuntimeStop();
    JsScripting::Unload();
}

TEST_CASE("TS instantiate(prefab) during Play creates scripts + physics; destroy(go, t) tears them down") {
    if (!LoadFixture()) return;
    const std::string prefabPath = (std::filesystem::temp_directory_path() / "duality_ts_spawned.prefab").string();
    {
        Scene authoring;
        Entity p = authoring.CreateEntity("Spawned");
        p.AddComponent<Rigidbody2DComponent>();
        p.AddComponent<BoxCollider2DComponent>().Size = { 0.5f, 0.5f };
        AddScript(p, "SpawnedProbe");
        CHECK(PrefabSerializer::Save(p, prefabPath));
    }
    const std::string guid = "feedfacefeedfacefeedfacefeedface";
    AssetDatabase::Register(guid, prefabPath);

    Scene scene;
    Entity spawner = scene.CreateEntity("Spawner");
    AddScript(spawner, "SpawnProbe").PropertyOverrides["prefab"] = AssetRef{ guid };
    scene.OnRuntimeStart();
    Step(scene, 1);
    const bool spawned = LogHas("spawnedAwake") && LogHas("spawned:Spawned") && LogHas("spawnedBody:true") && LogHas("spawnedY:-3");
    if (!spawned)
        std::printf("         log: %s\n", JsLog().c_str());
    CHECK_SOFT(spawned, "spawned prefab Awakes immediately and has a live Rigidbody2D body");
    Step(scene, 10);
    CHECK_SOFT(LogHas("spawnedDestroyed"), "destroy(go, 0.05) ran onDestroy");
    bool anyLeft = false;
    for (auto handle : scene.Registry().view<NameComponent>())
        anyLeft = anyLeft || scene.Registry().get<NameComponent>(handle).Name == "Spawned";
    CHECK_SOFT(!anyLeft, "the spawned entity is gone");
    scene.OnRuntimeStop();
    JsScripting::Unload();
    std::filesystem::remove(prefabPath);
}

TEST_CASE("TS coroutines: yield null / WaitForSeconds") {
    if (!LoadFixture()) return;
    Scene scene;
    AddScript(scene.CreateEntity("Co"), "CoroutineProbe");
    scene.OnRuntimeStart();
    Step(scene, 1);
    CHECK_SOFT(LogHas("co:begin") && !LogHas("co:frame"), "runs to the first yield inside start()");
    Step(scene, 1);
    CHECK_SOFT(LogHas("co:frame"), "yield null resumes next frame");
    Step(scene, 6);
    CHECK_SOFT(LogHas("co:waited:true"), "WaitForSeconds(0.05) resumed after ~3 frames");
    scene.OnRuntimeStop();
    JsScripting::Unload();
}

TEST_CASE("A TS exception is logged at the TypeScript line and disables only that script") {
    if (!LoadFixture()) return;
    Log::Clear();
    Scene scene;
    Entity e = scene.CreateEntity("Faulty");
    AddScript(e, "ErrorProbe");
    AddScript(e, "Mover");
    scene.OnRuntimeStart();
    Step(scene, 5);
    bool mapped = false, typeError = false;
    for (const LogEntry& entry : Log::GetEntries()) {
        if (entry.Level != LogLevel::Error)
            continue;
        typeError = typeError || entry.Message.find("TypeError") != std::string::npos;
        mapped = mapped || entry.Message.find("Assets/Scripts/Probes.ts:") != std::string::npos;
    }
    CHECK_SOFT(typeError, "the TypeError reached the Console");
    CHECK_SOFT(mapped, "stack trace points at Assets/Scripts/Probes.ts:<line> through the source map");
    const auto& scripts = e.GetComponent<BehaviourComponent>().Scripts;
    CHECK_SOFT(!scripts[0].Enabled, "faulty script disabled");
    CHECK_SOFT(scripts[1].Enabled && e.GetComponent<TransformComponent>().Translation.x > 0.0f, "the other script keeps running");
    scene.OnRuntimeStop();
    JsScripting::Unload();
}

TEST_CASE("TS bundle compiled to QuickJS bytecode loads and runs") {
    if (!*kFixture || !std::filesystem::exists(kFixture)) return;
    const std::string bytecode = std::string(kFixture) + "bc"; // scripts.js -> scripts.jsbc
    std::string error;
    CHECK(JsScripting::CompileToBytecode(kFixture, bytecode, &error));
    CHECK(JsScripting::LoadBundle(bytecode));
    CHECK_SOFT(ScriptRegistry::IsRegistered("LifecycleProbe"), "classes registered from bytecode");
    Scene scene;
    AddScript(scene.CreateEntity("Bc"), "LifecycleProbe");
    scene.OnRuntimeStart();
    Step(scene, 1);
    CHECK_SOFT(LogHas("awake:Bc") && LogHas("update"), "bytecode scripts run");
    scene.OnRuntimeStop();
    JsScripting::Unload();
}

namespace {
    // Same work as the TS Mover (translate by speed * dt), for an apples-to-apples baseline.
    struct NativeMover : public Behaviour {
        float Speed = 1.0f;
        void OnUpdate(float dt) override { GetTransform().Translate({ Speed * dt, 0.0f, 0.0f }); }
    };

    double MeasureMovers(const char* className, int count, int frames) {
        Scene scene;
        for (int i = 0; i < count; ++i)
            AddScript(scene.CreateEntity("M" + std::to_string(i)), className);
        scene.OnRuntimeStart();
        Step(scene, 5); // warm up (start(), first-call costs)
        const auto start = std::chrono::steady_clock::now();
        Step(scene, frames);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
        scene.OnRuntimeStop();
        return ms;
    }
}

TEST_CASE("TS update() overhead: 200 entities (vs the same C++ Behaviour)") {
    if (!LoadFixture()) return;
    ScriptRegistry::Register(ScriptFactoryEntry{ "NativeMover",
        []() -> Behaviour* { return new NativeMover(); }, [](Behaviour* b) { delete b; }, {} });
    const int frames = 300;
    const double empty = MeasureMovers("NoSuchScriptClass", 0, frames);
    const double cpp = MeasureMovers("NativeMover", 200, frames);
    const double ts = MeasureMovers("Mover", 200, frames);
    std::printf("         per frame: empty scene %.3f ms | 200 C++ movers %.3f ms | 200 TS movers %.3f ms\n", empty, cpp, ts);
    std::printf("         => TS update() costs ~%.2f us more than C++ per entity (desktop, unoptimized engine + -O2 QuickJS); JS heap %zu KB\n",
        (ts - cpp) * 1000.0 / 200.0, JsScripting::GetMemoryStats().MallocSize / 1024);
    CHECK_SOFT(ts < 50.0, "update loop stays interactive");
    JsScripting::Unload();
}

namespace {
    struct TsSettings : public ScriptableObject {
        float Health = 75.0f;
        int Coins = 5;
    };
}

TEST_CASE("TS reads/writes a C++ ScriptableObject asset through an @field(ScriptableObject)") {
    if (!LoadFixture()) return;
    if (!ScriptableObjectRegistry::Find("TsSettings")) {
        ScriptableObjectRegistry::Register(ScriptableObjectFactoryEntry{ "TsSettings",
            []() -> ScriptableObject* { return new TsSettings(); }, [](ScriptableObject* o) { delete o; },
            { MakeField("Health", &TsSettings::Health), MakeField("Coins", &TsSettings::Coins) } });
    }
    const std::string path = (std::filesystem::temp_directory_path() / "duality_ts_settings.asset").string();
    std::filesystem::remove(path);
    ScriptableObjectLoader::Loaded created = ScriptableObjectLoader::Create(path, "TsSettings");
    CHECK(created.Instance);
    const std::string guid = "abcdefabcdefabcdefabcdefabcdef01";
    AssetDatabase::Register(guid, path);

    Scene scene;
    AddScript(scene.CreateEntity("SoUser"), "SoProbe").PropertyOverrides["data"] = AssetRef{ guid };
    scene.OnRuntimeStart();
    Step(scene, 1);
    const bool read = LogHas("so:TsSettings|75|Health,Coins");
    if (!read)
        std::printf("         log: %s\n", JsLog().c_str());
    CHECK_SOFT(read, "class name, field value and field list read from TS");
    CHECK_SOFT(static_cast<TsSettings*>(ScriptableObjectLoader::Load(path).Instance)->Coins == 99, "TS wrote the field on the shared instance");
    scene.OnRuntimeStop();
    JsScripting::Unload();
    ScriptableObjectLoader::UnloadAll();
    std::filesystem::remove(path);
}

TEST_CASE("Overrides of a script class that isn't compiled yet survive load + save") {
    // E.g. the TypeScript bundle failed to build when the Editor opened the scene.
    nlohmann::json root = nlohmann::json::parse(R"({ "WorldUnitsVersion": 2, "CanvasUIVersion": 1, "Entities": [
        { "Name": { "Name": "E" }, "Parent": -1,
          "Scripts": [ { "ClassName": "NotCompiledYet", "Enabled": true, "PropertyOverrides": { "speed": 4.5, "label": "kept" } } ] } ] })");
    Scene scene;
    CHECK(SceneSerializer(scene).DeserializeFromJson(root));
    const std::string saved = SceneSerializer(scene).SerializeToJson().dump();
    CHECK_SOFT(saved.find("\"speed\":4.5") != std::string::npos && saved.find("\"label\":\"kept\"") != std::string::npos,
        "raw overrides written back verbatim");
}
