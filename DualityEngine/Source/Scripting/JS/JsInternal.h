#pragma once

// Internal state shared by JsRuntime.cpp (runtime lifetime, bundle loading, JsBehaviour) and
// JsBindings.cpp (the `__duality` native bridge). Not a public header: QuickJS types stay out
// of every other translation unit.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <quickjs.h>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scripting/JS/JsScripting.h"
#include "SourceMap.h"

namespace Duality::Js {

    // Lifecycle methods looked up ONCE per class on its prototype at registration, then
    // called with JS_Call every frame (no per-frame property lookup).
    enum Method : int {
        M_Awake, M_OnEnable, M_Start, M_Update, M_LateUpdate, M_FixedUpdate, M_OnDisable, M_OnDestroy,
        M_OnApplicationFocus, M_OnApplicationPause, M_OnApplicationQuit,
        M_Count
    };

    // Inspector field kinds, as declared by the TS runtime (duality.ts, describeFields).
    enum class FieldKind {
        Float, Int, Bool, String, Vec2, Vec3, Vec4, Color, Asset, Entity, Enum, AssetList, EntityList
    };

    struct FieldInfo {
        std::string Name;
        FieldKind Kind = FieldKind::Float;
        FieldValue Default;
    };

    struct ClassInfo {
        std::string Name;
        std::string SourceFile;
        JSValue Ctor = JS_UNDEFINED;
        JSValue Methods[M_Count];
        bool HasContactHandlers = false;
        std::vector<FieldInfo> Fields;
    };

    class JsBehaviour;

    struct State {
        JSRuntime* Runtime = nullptr;
        JSContext* Context = nullptr;
        JsRuntimeOptions Options;
        std::string LoadedPath;
        std::string BundleName;     // file name used in stack traces
        SourceMap Map;
        std::string MapPath;        // lazily loaded on the first error (3DS: keeps RAM free)
        bool MapLoaded = false;

        std::vector<std::unique_ptr<ClassInfo>> Classes;

        // Hooks installed by the TS runtime (__duality.init).
        JSValue HookCreate = JS_UNDEFINED;
        JSValue HookReadField = JS_UNDEFINED;
        JSValue HookWriteField = JS_UNDEFINED;
        JSValue HookContact = JS_UNDEFINED;
        JSValue HookSceneStopped = JS_UNDEFINED;
        JSValue HookTick = JS_UNDEFINED;
        JSValue ScratchArray = JS_UNDEFINED;
        double* Scratch = nullptr;  // Float64Array shared with the TS runtime
        size_t ScratchCount = 0;

        // Scene <-> small integer id ("slot"); an entity id seen by JS is slot * 2^32 + handle.
        std::vector<Scene*> Scenes{ nullptr };

        // Live JsBehaviours by key (JS objects store the key in `_k`).
        std::unordered_map<uint32_t, JsBehaviour*> Behaviours;
        uint32_t NextKey = 1;

        // Watchdog.
        double Deadline = 0.0;
        bool InFixedUpdate = false;
        uint64_t Generation = 0;
    };

    State& S();

    // --- helpers shared by both translation units ---------------------------------------
    double NowSeconds();
    uint32_t SceneSlot(Scene* scene);              // assigns a slot on first use
    double MakeEntityId(Entity entity);            // -1 for an invalid entity
    double MakeEntityId(Scene* scene, uint32_t handle);
    Entity EntityFromId(double id);                // empty Entity if stale/invalid
    Scene* SceneFromSlot(double slot);

    // Logs a pending exception as "[TS] <where>: <message>" + TS-mapped stack. Clears it.
    void ReportException(JSContext* ctx, const std::string& where);
    // JS_Call with the watchdog armed and microtasks drained afterwards. On exception: logs
    // and returns false (result already freed).
    bool CallJs(JSValueConst fn, JSValueConst thisObj, int argc, JSValueConst* argv, const std::string& where, JSValue* outResult = nullptr);

    // FieldValue <-> "normalized" JS value (numbers, booleans, strings, plain arrays for
    // vectors/colors, guid strings for assets, entity ids for entity refs). `scene` resolves
    // EntityRef handles to ids and back.
    JSValue FieldValueToJs(JSContext* ctx, const FieldValue& value, Scene* scene);
    bool JsToFieldValue(JSContext* ctx, JSValueConst value, const FieldValue& prototype, Scene* scene, FieldValue& out);

    void InstallBindings(JSContext* ctx);
    // registerClass/init (JsRuntime.cpp), installed onto the `__duality` object by InstallBindings.
    void InstallRuntimeNatives(JSContext* ctx, JSValueConst target);

    // One TypeScript script slot. Created by ScriptRegistry::TryCreate both for real Play
    // instances and for the Properties panel's per-frame scratch instances: the JS object is
    // only created in SetEngineServices (which Scene calls for real instances only), so a
    // scratch instance is a cheap C++ object serving Inspector fields from m_FieldCache.
    class JsBehaviour : public Behaviour {
    public:
        explicit JsBehaviour(ClassInfo* cls);
        ~JsBehaviour() override;

        void SetEngineServices(const EngineServices* services) override;

        void OnCreate() override;
        void OnEnable() override;
        void OnDisable() override;
        void OnUpdate(float deltaTime) override;
        void OnLateUpdate(float deltaTime) override;
        void OnFixedUpdate(float fixedDeltaTime) override;
        void OnDestroy() override;
        void OnApplicationFocus(bool focused) override;
        void OnApplicationPause(bool paused) override;
        void OnApplicationQuit() override;
        void OnCollisionEnter(Entity other) override { Contact(0, other); }
        void OnCollisionExit(Entity other) override { Contact(1, other); }
        void OnTriggerEnter(Entity other) override { Contact(2, other); }
        void OnTriggerExit(Entity other) override { Contact(3, other); }

        FieldValue GetField(size_t index);
        void SetField(size_t index, const FieldValue& value);

        bool IsLive() const { return m_Live; }
        JSValueConst Object() const { return m_Object; }
        ClassInfo* Class() const { return m_Class; }
        uint32_t Key() const { return m_Key; }
        bool CallNamed(const char* method);
        // Runtime is going away: free the JS object, become inert.
        void Detach();

        // Behaviour::IsEnabled/SetEnabled are public; this exposes the entity for bindings.
        Entity OwnerEntity() const { return GetEntity(); }

    private:
        bool Call(Method method, int argc = 0, JSValueConst* argv = nullptr);
        void EnsureStarted();
        void Contact(int type, Entity other);
        void Fault();

        ClassInfo* m_Class = nullptr;
        JSValue m_Object = JS_UNDEFINED;
        bool m_Live = false;
        bool m_Started = false;
        bool m_Faulted = false;
        uint32_t m_Key = 0;
        uint64_t m_Generation = 0;
        std::vector<FieldValue> m_FieldCache;
    };

    JsBehaviour* BehaviourFromKey(uint32_t key);

}
