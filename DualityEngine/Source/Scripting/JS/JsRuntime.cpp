// QuickJS-ng host for TypeScript scripting: runtime lifetime, bundle loading, error reporting
// (mapped back to TypeScript through the bundle's source map), class registration into
// ScriptRegistry, and JsBehaviour -- the Behaviour subclass every TS script instance is.
// See Include/DualityEngine/Scripting/JS/JsScripting.h for the overall design.

#include "JsInternal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <variant>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scripting/ScriptContext.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"
#include "JsCompile.h"

namespace Duality {

    JsRuntimeOptions JsRuntimeOptions::PlatformDefault() {
        JsRuntimeOptions options;
#if defined(DE_PLATFORM_3DS)
        // Old 3DS: ~64 MB for the whole application. 8 MB for scripts is generous for gameplay
        // code; collecting cycles every ~512 KB of new allocations keeps pauses short.
        options.MemoryLimitBytes = 8u * 1024u * 1024u;
        options.GcThresholdBytes = 512u * 1024u;
        options.MaxStackBytes = 192u * 1024u;
        options.CallTimeoutSeconds = 5.0;
#else
        options.MemoryLimitBytes = 0;
        options.GcThresholdBytes = 0;
        options.MaxStackBytes = 1024u * 1024u;
        options.CallTimeoutSeconds = 3.0;
#endif
        return options;
    }

}

namespace Duality::Js {

    State& S() {
        static State state;
        return state;
    }

    double NowSeconds() {
        using clock = std::chrono::steady_clock;
        static const clock::time_point start = clock::now();
        return std::chrono::duration<double>(clock::now() - start).count();
    }

    // --- Scenes / entity ids ---------------------------------------------------------------

    static constexpr double kSlotScale = 4294967296.0; // 2^32

    uint32_t SceneSlot(Scene* scene) {
        if (!scene)
            return 0;
        auto& scenes = S().Scenes;
        for (size_t i = 1; i < scenes.size(); ++i)
            if (scenes[i] == scene)
                return static_cast<uint32_t>(i);
        scenes.push_back(scene);
        return static_cast<uint32_t>(scenes.size() - 1);
    }

    double MakeEntityId(Scene* scene, uint32_t handle) {
        if (!scene)
            return -1.0;
        return static_cast<double>(SceneSlot(scene)) * kSlotScale + static_cast<double>(handle);
    }

    double MakeEntityId(Entity entity) {
        if (!entity.IsValid())
            return -1.0;
        return MakeEntityId(entity.GetScene(), static_cast<uint32_t>(entity.Handle()));
    }

    Scene* SceneFromSlot(double slot) {
        if (!(slot >= 1.0))
            return nullptr;
        const size_t index = static_cast<size_t>(slot);
        auto& scenes = S().Scenes;
        return index < scenes.size() ? scenes[index] : nullptr;
    }

    Entity EntityFromId(double id) {
        if (!(id >= kSlotScale) || !std::isfinite(id))
            return Entity{};
        const double slot = std::floor(id / kSlotScale);
        const uint32_t handle = static_cast<uint32_t>(id - slot * kSlotScale);
        Scene* scene = SceneFromSlot(slot);
        if (!scene)
            return Entity{};
        Entity entity(static_cast<entt::entity>(handle), scene);
        return entity.IsValid() ? entity : Entity{};
    }

    // --- Errors ----------------------------------------------------------------------------

    static void EnsureSourceMap() {
        State& s = S();
        if (s.MapLoaded)
            return;
        s.MapLoaded = true;
        if (s.MapPath.empty())
            return;
        std::ifstream file(s.MapPath, std::ios::binary);
        if (!file)
            return;
        std::stringstream buffer;
        buffer << file.rdbuf();
        s.Map.Parse(buffer.str());
    }

    static std::string ToStdString(JSContext* ctx, JSValueConst value) {
        const char* text = JS_ToCString(ctx, value);
        std::string result = text ? text : "";
        if (text)
            JS_FreeCString(ctx, text);
        return result;
    }

    void ReportException(JSContext* ctx, const std::string& where) {
        JSValue exception = JS_GetException(ctx);
        std::string message = ToStdString(ctx, exception);
        std::string stack;
        if (JS_IsError(exception)) {
            JSValue stackValue = JS_GetPropertyStr(ctx, exception, "stack");
            if (JS_IsString(stackValue))
                stack = ToStdString(ctx, stackValue);
            JS_FreeValue(ctx, stackValue);
        }
        JS_FreeValue(ctx, exception);

        std::string text = "[TS] " + where + ": " + message;
        if (!stack.empty()) {
            while (!stack.empty() && (stack.back() == '\n' || stack.back() == ' '))
                stack.pop_back();
            text += "\n" + JsScripting::RemapStack(stack);
        }
        Log::Error(text);
    }

    static int s_CallDepth = 0;

    static int InterruptHandler(JSRuntime*, void*) {
        const State& s = S();
        return (s.Deadline > 0.0 && NowSeconds() > s.Deadline) ? 1 : 0;
    }

    static void DrainJobs() {
        State& s = S();
        while (s.Runtime && JS_IsJobPending(s.Runtime)) {
            JSContext* jobContext = nullptr;
            if (JS_ExecutePendingJob(s.Runtime, &jobContext) < 0 && jobContext)
                ReportException(jobContext, "promise job");
        }
    }

    bool CallJs(JSValueConst fn, JSValueConst thisObj, int argc, JSValueConst* argv, const std::string& where, JSValue* outResult) {
        State& s = S();
        JSContext* ctx = s.Context;
        if (!ctx)
            return false;
        const bool outermost = (s_CallDepth == 0);
        if (outermost && s.Options.CallTimeoutSeconds > 0.0)
            s.Deadline = NowSeconds() + s.Options.CallTimeoutSeconds;
        ++s_CallDepth;
        JSValue result = JS_Call(ctx, fn, thisObj, argc, argv);
        --s_CallDepth;
        bool ok = !JS_IsException(result);
        if (!ok) {
            const bool timedOut = s.Deadline > 0.0 && NowSeconds() > s.Deadline;
            ReportException(ctx, timedOut ? where + " (interrupted: ran longer than the script watchdog allows)" : where);
        } else if (outResult) {
            *outResult = result;
        } else {
            JS_FreeValue(ctx, result);
        }
        if (outermost) {
            DrainJobs();
            s.Deadline = 0.0;
        }
        return ok;
    }

    static void PromiseRejectionTracker(JSContext* ctx, JSValueConst, JSValueConst reason, bool isHandled, void*) {
        if (isHandled)
            return;
        std::string message = ToStdString(ctx, reason);
        std::string stack;
        if (JS_IsError(reason)) {
            JSValue stackValue = JS_GetPropertyStr(ctx, reason, "stack");
            if (JS_IsString(stackValue))
                stack = ToStdString(ctx, stackValue);
            JS_FreeValue(ctx, stackValue);
        }
        Log::Error("[TS] unhandled promise rejection: " + message + (stack.empty() ? "" : "\n" + JsScripting::RemapStack(stack)));
    }

    // --- FieldValue <-> JS ------------------------------------------------------------------

    static JSValue NumberArray(JSContext* ctx, const float* values, int count) {
        JSValue array = JS_NewArray(ctx);
        for (int i = 0; i < count; ++i)
            JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), JS_NewFloat64(ctx, values[i]));
        return array;
    }

    static bool ReadNumberArray(JSContext* ctx, JSValueConst value, float* out, int count) {
        if (!JS_IsObject(value))
            return false;
        for (int i = 0; i < count; ++i) {
            JSValue element = JS_GetPropertyUint32(ctx, value, static_cast<uint32_t>(i));
            double number = 0.0;
            const bool ok = JS_ToFloat64(ctx, &number, element) == 0;
            JS_FreeValue(ctx, element);
            if (!ok)
                return false;
            out[i] = static_cast<float>(number);
        }
        return true;
    }

    static int64_t ArrayLength(JSContext* ctx, JSValueConst value) {
        int64_t length = 0;
        if (!JS_IsObject(value) || JS_GetLength(ctx, value, &length) < 0)
            return -1;
        return length;
    }

    template<typename T>
    static constexpr bool IsNativeEnum =
        std::is_same_v<T, Screen> || std::is_same_v<T, ProjectionType> || std::is_same_v<T, MeshPrimitive> ||
        std::is_same_v<T, UIAnchor> || std::is_same_v<T, BodyType> || std::is_same_v<T, Layer> ||
        std::is_same_v<T, CanvasRenderMode> || std::is_same_v<T, UILayoutType> || std::is_same_v<T, TextAlignment>;

    JSValue FieldValueToJs(JSContext* ctx, const FieldValue& value, Scene* scene) {
        return std::visit([&](auto&& v) -> JSValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int>) return JS_NewInt32(ctx, v);
            else if constexpr (std::is_same_v<T, uint32_t>) return JS_NewUint32(ctx, v);
            else if constexpr (std::is_same_v<T, float>) return JS_NewFloat64(ctx, v);
            else if constexpr (std::is_same_v<T, bool>) return JS_NewBool(ctx, v);
            else if constexpr (std::is_same_v<T, std::string>) return JS_NewStringLen(ctx, v.data(), v.size());
            else if constexpr (std::is_same_v<T, glm::vec2>) return NumberArray(ctx, &v.x, 2);
            else if constexpr (std::is_same_v<T, glm::vec3>) return NumberArray(ctx, &v.x, 3);
            else if constexpr (std::is_same_v<T, glm::vec4>) return NumberArray(ctx, &v.x, 4);
            else if constexpr (std::is_same_v<T, Color4>) return NumberArray(ctx, &v.Value.x, 4);
            else if constexpr (std::is_same_v<T, AssetRef>) return JS_NewStringLen(ctx, v.Guid.data(), v.Guid.size());
            else if constexpr (std::is_same_v<T, EntityRef>) {
                return JS_NewFloat64(ctx, v.Handle == EntityRef::Invalid || !scene ? -1.0 : MakeEntityId(scene, v.Handle));
            }
            else if constexpr (std::is_same_v<T, EnumFieldValue>) return JS_NewInt32(ctx, v.Value);
            else if constexpr (std::is_same_v<T, std::vector<AssetRef>>) {
                JSValue array = JS_NewArray(ctx);
                for (size_t i = 0; i < v.size(); ++i)
                    JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), JS_NewStringLen(ctx, v[i].Guid.data(), v[i].Guid.size()));
                return array;
            }
            else if constexpr (std::is_same_v<T, std::vector<EntityRef>>) {
                JSValue array = JS_NewArray(ctx);
                for (size_t i = 0; i < v.size(); ++i)
                    JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), JS_NewFloat64(ctx,
                        v[i].Handle == EntityRef::Invalid || !scene ? -1.0 : MakeEntityId(scene, v[i].Handle)));
                return array;
            }
            else if constexpr (IsNativeEnum<T>) return JS_NewInt32(ctx, static_cast<int>(v));
            else return JS_UNDEFINED; // NestedFieldValue: not exposed to scripts
        }, value);
    }

    static EntityRef EntityRefFromJs(JSContext* ctx, JSValueConst value, Scene* scene) {
        double id = -1.0;
        if (JS_IsNumber(value))
            JS_ToFloat64(ctx, &id, value);
        Entity entity = EntityFromId(id);
        if (!entity || (scene && entity.GetScene() != scene))
            return EntityRef{};
        return EntityRef{ static_cast<uint32_t>(entity.Handle()) };
    }

    bool JsToFieldValue(JSContext* ctx, JSValueConst value, const FieldValue& prototype, Scene* scene, FieldValue& out) {
        return std::visit([&](auto&& proto) -> bool {
            using T = std::decay_t<decltype(proto)>;
            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, uint32_t> || std::is_same_v<T, float> || IsNativeEnum<T>) {
                double number = 0.0;
                if (JS_ToFloat64(ctx, &number, value) != 0) { JS_FreeValue(ctx, JS_GetException(ctx)); return false; }
                if constexpr (std::is_same_v<T, float>) out = static_cast<float>(number);
                else if constexpr (std::is_same_v<T, int>) out = static_cast<int>(std::lround(number));
                else if constexpr (std::is_same_v<T, uint32_t>) out = static_cast<uint32_t>(number < 0 ? 0 : std::llround(number));
                else out = static_cast<T>(static_cast<int>(std::lround(number)));
                return true;
            }
            else if constexpr (std::is_same_v<T, bool>) {
                const int truthy = JS_ToBool(ctx, value);
                if (truthy < 0) return false;
                out = truthy != 0;
                return true;
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                if (JS_IsUndefined(value) || JS_IsNull(value)) { out = std::string(); return true; }
                out = ToStdString(ctx, value);
                return true;
            }
            else if constexpr (std::is_same_v<T, glm::vec2>) { glm::vec2 v{}; if (!ReadNumberArray(ctx, value, &v.x, 2)) return false; out = v; return true; }
            else if constexpr (std::is_same_v<T, glm::vec3>) { glm::vec3 v{}; if (!ReadNumberArray(ctx, value, &v.x, 3)) return false; out = v; return true; }
            else if constexpr (std::is_same_v<T, glm::vec4>) { glm::vec4 v{}; if (!ReadNumberArray(ctx, value, &v.x, 4)) return false; out = v; return true; }
            else if constexpr (std::is_same_v<T, Color4>) { Color4 c{}; if (!ReadNumberArray(ctx, value, &c.Value.x, 4)) return false; out = c; return true; }
            else if constexpr (std::is_same_v<T, AssetRef>) {
                out = AssetRef{ JS_IsString(value) ? ToStdString(ctx, value) : std::string() };
                return true;
            }
            else if constexpr (std::is_same_v<T, EntityRef>) { out = EntityRefFromJs(ctx, value, scene); return true; }
            else if constexpr (std::is_same_v<T, EnumFieldValue>) {
                double number = 0.0;
                if (JS_ToFloat64(ctx, &number, value) != 0) { JS_FreeValue(ctx, JS_GetException(ctx)); return false; }
                EnumFieldValue result = proto;
                result.Value = static_cast<int>(std::lround(number));
                if (!result.Options.empty())
                    result.Value = std::clamp(result.Value, 0, static_cast<int>(result.Options.size()) - 1);
                out = result;
                return true;
            }
            else if constexpr (std::is_same_v<T, std::vector<AssetRef>>) {
                std::vector<AssetRef> list;
                const int64_t length = ArrayLength(ctx, value);
                for (int64_t i = 0; i < length; ++i) {
                    JSValue element = JS_GetPropertyUint32(ctx, value, static_cast<uint32_t>(i));
                    list.push_back(AssetRef{ JS_IsString(element) ? ToStdString(ctx, element) : std::string() });
                    JS_FreeValue(ctx, element);
                }
                out = std::move(list);
                return true;
            }
            else if constexpr (std::is_same_v<T, std::vector<EntityRef>>) {
                std::vector<EntityRef> list;
                const int64_t length = ArrayLength(ctx, value);
                for (int64_t i = 0; i < length; ++i) {
                    JSValue element = JS_GetPropertyUint32(ctx, value, static_cast<uint32_t>(i));
                    list.push_back(EntityRefFromJs(ctx, element, scene));
                    JS_FreeValue(ctx, element);
                }
                out = std::move(list);
                return true;
            }
            else return false;
        }, prototype);
    }

    // --- Class registration (called by the bundle through __duality.registerClass) ---------

    static const char* const kMethodNames[M_Count] = {
        "awake", "onEnable", "start", "update", "lateUpdate", "fixedUpdate", "onDisable", "onDestroy",
        "onApplicationFocus", "onApplicationPause", "onApplicationQuit",
    };

    static bool ParseFieldKind(const std::string& text, FieldKind& out) {
        static const std::pair<const char*, FieldKind> kinds[] = {
            { "float", FieldKind::Float }, { "int", FieldKind::Int }, { "bool", FieldKind::Bool },
            { "string", FieldKind::String }, { "vec2", FieldKind::Vec2 }, { "vec3", FieldKind::Vec3 },
            { "vec4", FieldKind::Vec4 }, { "color", FieldKind::Color }, { "asset", FieldKind::Asset },
            { "entity", FieldKind::Entity }, { "enum", FieldKind::Enum }, { "assets", FieldKind::AssetList },
            { "entities", FieldKind::EntityList },
        };
        for (const auto& [name, kind] : kinds) {
            if (text == name) {
                out = kind;
                return true;
            }
        }
        return false;
    }

    static FieldValue PrototypeFor(FieldKind kind) {
        switch (kind) {
            case FieldKind::Float: return 0.0f;
            case FieldKind::Int: return 0;
            case FieldKind::Bool: return false;
            case FieldKind::String: return std::string();
            case FieldKind::Vec2: return glm::vec2(0.0f);
            case FieldKind::Vec3: return glm::vec3(0.0f);
            case FieldKind::Vec4: return glm::vec4(0.0f);
            case FieldKind::Color: return Color4{ glm::vec4(1.0f) };
            case FieldKind::Asset: return AssetRef{};
            case FieldKind::Entity: return EntityRef{};
            case FieldKind::Enum: return EnumFieldValue{};
            case FieldKind::AssetList: return std::vector<AssetRef>{};
            case FieldKind::EntityList: return std::vector<EntityRef>{};
        }
        return 0.0f;
    }

    static JSValue NativeRegisterClass(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 3 || !JS_IsString(argv[0]) || !JS_IsFunction(ctx, argv[1]))
            return JS_ThrowTypeError(ctx, "registerClass(name, ctor, fields, file)");
        State& s = S();
        auto cls = std::make_unique<ClassInfo>();
        cls->Name = ToStdString(ctx, argv[0]);
        cls->SourceFile = argc > 3 && JS_IsString(argv[3]) ? ToStdString(ctx, argv[3]) : std::string();
        cls->Ctor = JS_DupValue(ctx, argv[1]);
        for (auto& method : cls->Methods)
            method = JS_UNDEFINED;

        JSValue prototype = JS_GetPropertyStr(ctx, argv[1], "prototype");
        for (int i = 0; i < M_Count; ++i) {
            JSValue fn = JS_GetPropertyStr(ctx, prototype, kMethodNames[i]);
            if (JS_IsFunction(ctx, fn))
                cls->Methods[i] = fn;
            else
                JS_FreeValue(ctx, fn);
        }
        static const char* const contactNames[] = {
            "onCollisionEnter", "onCollisionExit", "onTriggerEnter", "onTriggerExit",
            "onCollisionEnter2D", "onCollisionExit2D", "onTriggerEnter2D", "onTriggerExit2D",
        };
        for (const char* name : contactNames) {
            JSValue fn = JS_GetPropertyStr(ctx, prototype, name);
            if (JS_IsFunction(ctx, fn))
                cls->HasContactHandlers = true;
            JS_FreeValue(ctx, fn);
        }
        JS_FreeValue(ctx, prototype);

        // fields: [{ name, kind, def, options?, range?, tooltip?, header? }, ...]
        std::vector<FieldHandle> handles;
        const int64_t fieldCount = ArrayLength(ctx, argv[2]);
        for (int64_t i = 0; i < fieldCount; ++i) {
            JSValue descriptor = JS_GetPropertyUint32(ctx, argv[2], static_cast<uint32_t>(i));
            JSValue nameValue = JS_GetPropertyStr(ctx, descriptor, "name");
            JSValue kindValue = JS_GetPropertyStr(ctx, descriptor, "kind");
            JSValue defaultValue = JS_GetPropertyStr(ctx, descriptor, "def");
            FieldInfo info;
            info.Name = ToStdString(ctx, nameValue);
            FieldKind kind;
            if (!ParseFieldKind(ToStdString(ctx, kindValue), kind)) {
                Log::Warn("[TS] " + cls->Name + "." + info.Name + ": unsupported field kind, not shown in the Inspector");
            } else {
                info.Kind = kind;
                FieldValue prototypeValue = PrototypeFor(kind);
                if (kind == FieldKind::Enum) {
                    EnumFieldValue enumValue;
                    JSValue options = JS_GetPropertyStr(ctx, descriptor, "options");
                    const int64_t optionCount = ArrayLength(ctx, options);
                    for (int64_t o = 0; o < optionCount; ++o) {
                        JSValue option = JS_GetPropertyUint32(ctx, options, static_cast<uint32_t>(o));
                        enumValue.Options.push_back(ToStdString(ctx, option));
                        JS_FreeValue(ctx, option);
                    }
                    JS_FreeValue(ctx, options);
                    prototypeValue = enumValue;
                }
                info.Default = prototypeValue;
                FieldValue converted;
                if (JsToFieldValue(ctx, defaultValue, prototypeValue, nullptr, converted))
                    info.Default = converted;

                FieldHandle handle;
                handle.Name = info.Name;
                JSValue range = JS_GetPropertyStr(ctx, descriptor, "range");
                float rangeValues[2];
                if (ArrayLength(ctx, range) == 2 && ReadNumberArray(ctx, range, rangeValues, 2)) {
                    handle.HasRange = true;
                    handle.RangeMin = rangeValues[0];
                    handle.RangeMax = rangeValues[1];
                }
                JS_FreeValue(ctx, range);
                JSValue tooltip = JS_GetPropertyStr(ctx, descriptor, "tooltip");
                if (JS_IsString(tooltip)) handle.Tooltip = ToStdString(ctx, tooltip);
                JS_FreeValue(ctx, tooltip);
                JSValue header = JS_GetPropertyStr(ctx, descriptor, "header");
                if (JS_IsString(header)) handle.Header = ToStdString(ctx, header);
                JS_FreeValue(ctx, header);

                ClassInfo* owner = cls.get();
                const size_t index = cls->Fields.size();
                handle.Get = [owner, index](void* instance) -> FieldValue {
                    auto* behaviour = static_cast<JsBehaviour*>(static_cast<Behaviour*>(instance));
                    if (behaviour->Class() != owner)
                        return owner->Fields[index].Default;
                    return behaviour->GetField(index);
                };
                handle.Set = [owner, index](void* instance, const FieldValue& value) {
                    auto* behaviour = static_cast<JsBehaviour*>(static_cast<Behaviour*>(instance));
                    if (behaviour->Class() == owner)
                        behaviour->SetField(index, value);
                };
                cls->Fields.push_back(std::move(info));
                handles.push_back(std::move(handle));
            }
            JS_FreeValue(ctx, nameValue);
            JS_FreeValue(ctx, kindValue);
            JS_FreeValue(ctx, defaultValue);
            JS_FreeValue(ctx, descriptor);
        }

        ClassInfo* owner = cls.get();
        const bool registered = ScriptRegistry::RegisterDynamic(cls->Name, ScriptLanguage::TypeScript,
            [owner]() -> Behaviour* { return new JsBehaviour(owner); },
            [](Behaviour* instance) { delete instance; },
            std::move(handles));
        if (!registered) {
            for (auto& method : cls->Methods)
                JS_FreeValue(ctx, method);
            JS_FreeValue(ctx, cls->Ctor);
            return JS_NewBool(ctx, false);
        }
        s.Classes.push_back(std::move(cls));
        return JS_NewBool(ctx, true);
    }

    // __duality.init(hooks, scratch): the TS runtime hands over the functions C++ calls back.
    static JSValue NativeInit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        if (argc < 2 || !JS_IsObject(argv[0]))
            return JS_ThrowTypeError(ctx, "init(hooks, scratch)");
        State& s = S();
        auto take = [&](JSValue& slot, const char* name) {
            JS_FreeValue(ctx, slot);
            slot = JS_GetPropertyStr(ctx, argv[0], name);
        };
        take(s.HookCreate, "create");
        take(s.HookReadField, "readField");
        take(s.HookWriteField, "writeField");
        take(s.HookContact, "contact");
        take(s.HookSceneStopped, "sceneStopped");
        take(s.HookTick, "tick");
        take(s.HookEvent, "event");

        size_t offset = 0, length = 0, bytesPerElement = 0;
        JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &length, &bytesPerElement);
        if (JS_IsException(buffer) || bytesPerElement != sizeof(double))
            return JS_ThrowTypeError(ctx, "init: scratch must be a Float64Array");
        size_t bufferSize = 0;
        uint8_t* data = JS_GetArrayBuffer(ctx, &bufferSize, buffer);
        JS_FreeValue(ctx, buffer);
        JS_FreeValue(ctx, s.ScratchArray);
        s.ScratchArray = JS_DupValue(ctx, argv[1]);
        s.Scratch = data ? reinterpret_cast<double*>(data + offset) : nullptr;
        s.ScratchCount = length / sizeof(double);
        return JS_UNDEFINED;
    }

    void InstallRuntimeNatives(JSContext* ctx, JSValueConst target) {
        JS_SetPropertyStr(ctx, target, "registerClass", JS_NewCFunction(ctx, NativeRegisterClass, "registerClass", 4));
        JS_SetPropertyStr(ctx, target, "init", JS_NewCFunction(ctx, NativeInit, "init", 2));
    }

    // --- JsBehaviour -------------------------------------------------------------------------

    JsBehaviour* BehaviourFromKey(uint32_t key) {
        auto& map = S().Behaviours;
        auto it = map.find(key);
        return it != map.end() ? it->second : nullptr;
    }

    JsBehaviour::JsBehaviour(ClassInfo* cls) : m_Class(cls), m_Generation(S().Generation) {
        m_FieldCache.reserve(cls ? cls->Fields.size() : 0);
        if (cls)
            for (const FieldInfo& field : cls->Fields)
                m_FieldCache.push_back(field.Default);
    }

    JsBehaviour::~JsBehaviour() {
        Detach();
    }

    void JsBehaviour::Detach() {
        State& s = S();
        if (m_Key) {
            s.Behaviours.erase(m_Key);
            m_Key = 0;
        }
        if (m_Live && m_Generation == s.Generation && s.Context) {
            // A script object some other script still holds becomes inert ("destroyed").
            JS_SetPropertyStr(s.Context, m_Object, "_k", JS_NewInt32(s.Context, 0));
            JS_FreeValue(s.Context, m_Object);
        }
        m_Object = JS_UNDEFINED;
        m_Live = false;
    }

    void JsBehaviour::SetEngineServices(const EngineServices* services) {
        Behaviour::SetEngineServices(services);
        State& s = S();
        if (m_Live || !m_Class || m_Generation != s.Generation || !s.Context || !JS_IsFunction(s.Context, s.HookCreate))
            return;
        m_Key = s.NextKey++;
        if (s.NextKey == 0)
            s.NextKey = 1;
        s.Behaviours[m_Key] = this;

        JSContext* ctx = s.Context;
        // The script constructor (field initializers) may touch this.gameObject: the context
        // must be bound to this entity while it runs, exactly like a lifecycle callback.
        Entity entity = GetEntity();
        const bool bindContext = ScriptContext::Scene() == nullptr && entity;
        if (bindContext)
            ScriptContext::Bind(services, entity.GetScene(), static_cast<unsigned int>(entity.Handle()));
        JSValue args[3] = { m_Class->Ctor, JS_NewFloat64(ctx, MakeEntityId(entity)), JS_NewUint32(ctx, m_Key) };
        JSValue object = JS_UNDEFINED;
        const bool ok = CallJs(s.HookCreate, JS_UNDEFINED, 3, args, m_Class->Name + " constructor", &object);
        if (bindContext)
            ScriptContext::Clear();
        if (!ok || !JS_IsObject(object)) {
            JS_FreeValue(ctx, object);
            s.Behaviours.erase(m_Key);
            m_Key = 0;
            Fault();
            return;
        }
        m_Object = object;
        m_Live = true;
    }

    void JsBehaviour::Fault() {
        m_Faulted = true;
        SetEnabled(false);
        Log::Warn("[TS] script '" + (m_Class ? m_Class->Name : std::string("?")) + "' disabled for this Play session after an error");
    }

    bool JsBehaviour::Call(Method method, int argc, JSValueConst* argv) {
        if (!m_Live || m_Faulted || m_Generation != S().Generation)
            return false;
        JSValueConst fn = m_Class->Methods[method];
        if (JS_IsUndefined(fn))
            return true;
        if (!CallJs(fn, m_Object, argc, argv, m_Class->Name + "." + kMethodNames[method])) {
            Fault();
            return false;
        }
        return true;
    }

    void JsBehaviour::EnsureStarted() {
        if (m_Started)
            return;
        m_Started = true;
        Call(M_Start);
    }

    void JsBehaviour::OnCreate() { Call(M_Awake); }
    void JsBehaviour::OnEnable() { Call(M_OnEnable); }
    void JsBehaviour::OnDisable() { Call(M_OnDisable); }
    void JsBehaviour::OnDestroy() { Call(M_OnDestroy); }

    void JsBehaviour::OnUpdate(float deltaTime) {
        if (!m_Live || m_Faulted)
            return;
        EnsureStarted();
        if (m_Faulted || !IsEnabled())
            return;
        JSValue arg = JS_NewFloat64(S().Context, deltaTime);
        Call(M_Update, 1, &arg);
    }

    void JsBehaviour::OnLateUpdate(float deltaTime) {
        if (!m_Live || m_Faulted || !m_Started)
            return;
        JSValue arg = JS_NewFloat64(S().Context, deltaTime);
        Call(M_LateUpdate, 1, &arg);
    }

    void JsBehaviour::OnFixedUpdate(float fixedDeltaTime) {
        if (!m_Live || m_Faulted)
            return;
        EnsureStarted();
        if (m_Faulted || !IsEnabled())
            return;
        State& s = S();
        s.InFixedUpdate = true;
        JSValue arg = JS_NewFloat64(s.Context, fixedDeltaTime);
        Call(M_FixedUpdate, 1, &arg);
        s.InFixedUpdate = false;
    }

    void JsBehaviour::OnApplicationFocus(bool focused) {
        JSValue arg = JS_NewBool(S().Context, focused);
        Call(M_OnApplicationFocus, 1, &arg);
    }
    void JsBehaviour::OnApplicationPause(bool paused) {
        JSValue arg = JS_NewBool(S().Context, paused);
        Call(M_OnApplicationPause, 1, &arg);
    }
    void JsBehaviour::OnApplicationQuit() { Call(M_OnApplicationQuit); }

    void JsBehaviour::Contact(int type, Entity other) {
        State& s = S();
        if (!m_Live || m_Faulted || !m_Class->HasContactHandlers || !JS_IsFunction(s.Context, s.HookContact))
            return;
        const bool is2D = other.HasComponent<Rigidbody2DComponent>() || other.HasComponent<BoxCollider2DComponent>() ||
            other.HasComponent<CircleCollider2DComponent>() || other.HasComponent<CapsuleCollider2DComponent>() ||
            other.HasComponent<PolygonCollider2DComponent>() || other.HasComponent<EdgeCollider2DComponent>();
        JSContext* ctx = s.Context;
        JSValue args[4] = { m_Object, JS_NewInt32(ctx, type), JS_NewFloat64(ctx, MakeEntityId(other)), JS_NewBool(ctx, is2D) };
        static const char* const names[] = { "onCollisionEnter", "onCollisionExit", "onTriggerEnter", "onTriggerExit" };
        if (!CallJs(s.HookContact, JS_UNDEFINED, 4, args, m_Class->Name + "." + names[type & 3] + (is2D ? "2D" : "")))
            Fault();
    }

    // Engine events with arguments: only dispatched when the script class defines `name`, so a
    // class without the handler costs one property lookup (no JS call).
    void JsBehaviour::Event(const char* name, Entity other, const double* numbers, int count) {
        State& s = S();
        if (!m_Live || m_Faulted || !JS_IsFunction(s.Context, s.HookEvent))
            return;
        JSContext* ctx = s.Context;
        JSValue handler = JS_GetPropertyStr(ctx, m_Object, name);
        const bool defined = JS_IsFunction(ctx, handler);
        JS_FreeValue(ctx, handler);
        if (!defined)
            return;
        JSValue args[14];
        int argc = 0;
        args[argc++] = m_Object;
        args[argc++] = JS_NewString(ctx, name);
        args[argc++] = JS_NewFloat64(ctx, other ? MakeEntityId(other) : -1.0);
        for (int i = 0; i < count && argc < 14; ++i)
            args[argc++] = JS_NewFloat64(ctx, numbers[i]);
        const bool ok = CallJs(s.HookEvent, JS_UNDEFINED, argc, args, m_Class->Name + "." + name);
        JS_FreeValue(ctx, args[1]);
        if (!ok)
            Fault();
    }

    void JsBehaviour::OnJointBreak2D(Entity joint, float breakForce) {
        const double value = breakForce;
        Event("onJointBreak2D", joint, &value, 1);
    }

    void JsBehaviour::OnJointBreak(Entity joint, float breakForce) {
        const double value = breakForce;
        Event("onJointBreak", joint, &value, 1);
    }

    void JsBehaviour::OnControllerColliderHit(const ControllerColliderHit& hit) {
        const double values[] = { hit.Point.x, hit.Point.y, hit.Point.z, hit.Normal.x, hit.Normal.y, hit.Normal.z,
                                  hit.MoveDirection.x, hit.MoveDirection.y, hit.MoveDirection.z, hit.MoveLength };
        Event("onControllerColliderHit", hit.Other, values, 10);
    }

    bool JsBehaviour::CallNamed(const char* method) {
        State& s = S();
        if (!m_Live || m_Faulted || !method)
            return false;
        JSValue fn = JS_GetPropertyStr(s.Context, m_Object, method);
        bool ok = false;
        if (JS_IsFunction(s.Context, fn))
            ok = CallJs(fn, m_Object, 0, nullptr, m_Class->Name + "." + method);
        JS_FreeValue(s.Context, fn);
        return ok;
    }

    FieldValue JsBehaviour::GetField(size_t index) {
        State& s = S();
        if (!m_Class || index >= m_Class->Fields.size())
            return 0.0f;
        const FieldInfo& field = m_Class->Fields[index];
        if (!m_Live || m_Generation != s.Generation || !JS_IsFunction(s.Context, s.HookReadField))
            return index < m_FieldCache.size() ? m_FieldCache[index] : field.Default;

        JSContext* ctx = s.Context;
        JSValue args[3] = { m_Object, JS_NewStringLen(ctx, field.Name.data(), field.Name.size()), JS_NewInt32(ctx, static_cast<int>(field.Kind)) };
        JSValue result = JS_UNDEFINED;
        FieldValue value = field.Default;
        if (CallJs(s.HookReadField, JS_UNDEFINED, 3, args, m_Class->Name + " read field '" + field.Name + "'", &result)) {
            FieldValue converted;
            if (JsToFieldValue(ctx, result, field.Default, GetEntity().GetScene(), converted))
                value = converted;
            JS_FreeValue(ctx, result);
        }
        JS_FreeValue(ctx, args[1]);
        return value;
    }

    void JsBehaviour::SetField(size_t index, const FieldValue& value) {
        State& s = S();
        if (!m_Class || index >= m_Class->Fields.size())
            return;
        const FieldInfo& field = m_Class->Fields[index];
        if (value.index() != field.Default.index())
            return; // a stale override of a different type (field changed type in the .ts)
        if (!m_Live || m_Generation != s.Generation || !JS_IsFunction(s.Context, s.HookWriteField)) {
            if (index < m_FieldCache.size())
                m_FieldCache[index] = value;
            return;
        }
        JSContext* ctx = s.Context;
        JSValue args[4] = {
            m_Object,
            JS_NewStringLen(ctx, field.Name.data(), field.Name.size()),
            JS_NewInt32(ctx, static_cast<int>(field.Kind)),
            FieldValueToJs(ctx, value, GetEntity().GetScene()),
        };
        CallJs(s.HookWriteField, JS_UNDEFINED, 4, args, m_Class->Name + " write field '" + field.Name + "'");
        JS_FreeValue(ctx, args[1]);
        JS_FreeValue(ctx, args[3]);
    }

}

namespace Duality {

    using namespace Js;

    static void FreeState() {
        State& s = S();
        // Detach every live instance first: their JSValues must be released before the
        // runtime is freed.
        std::vector<JsBehaviour*> live;
        for (auto& [key, behaviour] : s.Behaviours)
            live.push_back(behaviour);
        for (JsBehaviour* behaviour : live)
            behaviour->Detach();
        s.Behaviours.clear();

        ScriptRegistry::ClearLanguage(ScriptLanguage::TypeScript);

        if (s.Context) {
            for (auto& cls : s.Classes) {
                for (auto& method : cls->Methods)
                    JS_FreeValue(s.Context, method);
                JS_FreeValue(s.Context, cls->Ctor);
            }
            JS_FreeValue(s.Context, s.HookCreate);
            JS_FreeValue(s.Context, s.HookReadField);
            JS_FreeValue(s.Context, s.HookWriteField);
            JS_FreeValue(s.Context, s.HookContact);
            JS_FreeValue(s.Context, s.HookSceneStopped);
            JS_FreeValue(s.Context, s.HookTick);
            JS_FreeValue(s.Context, s.HookEvent);
            JS_FreeValue(s.Context, s.ScratchArray);
        }
        s.Classes.clear();
        s.HookCreate = s.HookReadField = s.HookWriteField = s.HookContact = s.HookSceneStopped = s.HookTick = s.HookEvent = JS_UNDEFINED;
        s.ScratchArray = JS_UNDEFINED;
        s.Scratch = nullptr;
        s.ScratchCount = 0;

        if (s.Context) {
            JS_FreeContext(s.Context);
            s.Context = nullptr;
        }
        if (s.Runtime) {
            JS_FreeRuntime(s.Runtime);
            s.Runtime = nullptr;
        }
        s.Scenes.assign(1, nullptr);
        s.LoadedPath.clear();
        s.BundleName.clear();
        s.Map.Clear();
        s.MapPath.clear();
        s.MapLoaded = false;
        s.Deadline = 0.0;
        s.InFixedUpdate = false;
        ++s.Generation;
    }

    void JsScripting::Unload() {
        FreeState();
    }

    bool JsScripting::IsLoaded() {
        return S().Context != nullptr;
    }

    const std::string& JsScripting::GetLoadedPath() {
        return S().LoadedPath;
    }

    std::vector<std::string> JsScripting::GetClassNames() {
        std::vector<std::string> names;
        for (auto& cls : S().Classes)
            names.push_back(cls->Name);
        return names;
    }

    std::string JsScripting::GetClassSourceFile(const std::string& className) {
        for (auto& cls : S().Classes)
            if (cls->Name == className)
                return cls->SourceFile;
        return {};
    }

    size_t JsScripting::GetLiveInstanceCount() {
        return S().Behaviours.size();
    }

    JsMemoryStats JsScripting::GetMemoryStats() {
        JsMemoryStats stats;
        if (!S().Runtime)
            return stats;
        JSMemoryUsage usage;
        JS_ComputeMemoryUsage(S().Runtime, &usage);
        stats.MallocSize = static_cast<size_t>(usage.malloc_size);
        stats.MallocLimit = usage.malloc_limit > 0 ? static_cast<size_t>(usage.malloc_limit) : 0;
        stats.ObjectCount = static_cast<size_t>(usage.obj_count);
        stats.FunctionCount = static_cast<size_t>(usage.js_func_count);
        stats.AtomCount = static_cast<size_t>(usage.atom_count);
        return stats;
    }

    void JsScripting::RunGC() {
        if (S().Runtime)
            JS_RunGC(S().Runtime);
    }

    static bool ReadFile(const std::string& path, std::string& out) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        out = buffer.str();
        return true;
    }

    static std::string FileNameOf(const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const size_t colon = name.find(':'); // "romfs:/x" style roots
        return colon == std::string::npos ? name : name.substr(colon + 1);
    }

    static bool CreateRuntime(const JsRuntimeOptions& options) {
        FreeState();
        State& s = S();
        s.Options = options;
        s.Runtime = JS_NewRuntime();
        if (!s.Runtime) {
            Log::Error("[TS] JS_NewRuntime failed");
            return false;
        }
        if (options.MemoryLimitBytes)
            JS_SetMemoryLimit(s.Runtime, options.MemoryLimitBytes);
        if (options.GcThresholdBytes)
            JS_SetGCThreshold(s.Runtime, options.GcThresholdBytes);
        if (options.MaxStackBytes)
            JS_SetMaxStackSize(s.Runtime, options.MaxStackBytes);
        JS_SetInterruptHandler(s.Runtime, InterruptHandler, nullptr);
        JS_SetHostPromiseRejectionTracker(s.Runtime, PromiseRejectionTracker, nullptr);
        s.Context = JS_NewContext(s.Runtime);
        if (!s.Context) {
            Log::Error("[TS] JS_NewContext failed");
            FreeState();
            return false;
        }
        InstallBindings(s.Context);
        return true;
    }

    static bool FinishLoad(bool ok, const std::string& what) {
        State& s = S();
        if (!ok) {
            FreeState();
            return false;
        }
        JsMemoryStats stats = JsScripting::GetMemoryStats();
        Log::Info("[TS] loaded " + what + ": " + std::to_string(s.Classes.size()) + " script class(es), JS heap " +
            std::to_string(stats.MallocSize / 1024) + " KB");
        return true;
    }

    bool JsScripting::LoadSource(const std::string& source, const std::string& displayName,
                                 const std::string& sourceMapJson, const JsRuntimeOptions& options) {
        if (!CreateRuntime(options))
            return false;
        State& s = S();
        s.BundleName = displayName;
        if (!sourceMapJson.empty()) {
            s.Map.Parse(sourceMapJson);
            s.MapLoaded = true;
        }
        const double start = NowSeconds();
        s.Deadline = options.CallTimeoutSeconds > 0.0 ? NowSeconds() + options.CallTimeoutSeconds * 4.0 : 0.0;
        JSValue result = JS_Eval(s.Context, source.c_str(), source.size(), displayName.c_str(), JS_EVAL_TYPE_GLOBAL);
        s.Deadline = 0.0;
        const bool ok = !JS_IsException(result);
        if (!ok)
            ReportException(s.Context, "loading " + displayName);
        else
            JS_FreeValue(s.Context, result);
        DrainJobs();
        if (ok)
            Log::Trace("[TS] evaluated " + displayName + " in " + std::to_string(static_cast<int>((NowSeconds() - start) * 1000.0)) + " ms");
        return FinishLoad(ok, displayName);
    }

    bool JsScripting::LoadBundle(const std::string& path, const JsRuntimeOptions& options) {
        const bool isBytecode = path.size() > 5 && path.compare(path.size() - 5, 5, ".jsbc") == 0;
        std::string data;
        if (!ReadFile(path, data)) {
            Log::Error("[TS] script bundle not found: " + path);
            FreeState();
            return false;
        }
        // Source map: "<x>.js.map" next to "<x>.js" (or next to the .js a .jsbc was built from).
        const std::string jsPath = isBytecode ? path.substr(0, path.size() - 2) : path; // .jsbc -> .js
        const std::string mapPath = jsPath + ".map";

        if (!isBytecode) {
            // Desktop: the map is read lazily too -- only an error ever needs it.
            if (!LoadSource(data, FileNameOf(path), std::string(), options))
                return false;
            S().MapPath = mapPath;
            S().MapLoaded = false;
            S().LoadedPath = path;
            return true;
        }

        if (!CreateRuntime(options))
            return false;
        State& s = S();
        s.BundleName = FileNameOf(jsPath);
        s.MapPath = mapPath;
        s.MapLoaded = false;
        const double start = NowSeconds();
        JSValue function = JS_ReadObject(s.Context, reinterpret_cast<const uint8_t*>(data.data()), data.size(), JS_READ_OBJ_BYTECODE);
        bool ok = !JS_IsException(function);
        if (!ok) {
            ReportException(s.Context, "reading bytecode " + path);
        } else {
            s.Deadline = options.CallTimeoutSeconds > 0.0 ? NowSeconds() + options.CallTimeoutSeconds * 4.0 : 0.0;
            JSValue result = JS_EvalFunction(s.Context, function); // consumes `function`
            s.Deadline = 0.0;
            ok = !JS_IsException(result);
            if (!ok)
                ReportException(s.Context, "running " + path);
            else
                JS_FreeValue(s.Context, result);
            DrainJobs();
        }
        if (ok)
            Log::Trace("[TS] loaded bytecode " + path + " in " + std::to_string(static_cast<int>((NowSeconds() - start) * 1000.0)) + " ms");
        if (!FinishLoad(ok, path))
            return false;
        S().LoadedPath = path;
        return true;
    }

    bool JsScripting::CompileToBytecode(const std::string& jsPath, const std::string& outPath, std::string* outError) {
        std::string source;
        if (!ReadFile(jsPath, source)) {
            if (outError) *outError = "cannot read " + jsPath;
            return false;
        }
        std::vector<unsigned char> bytecode;
        if (!CompileScriptToBytecode(source, FileNameOf(jsPath), bytecode, outError))
            return false;
        std::ofstream out(outPath, std::ios::binary);
        if (!out) {
            if (outError) *outError = "cannot write " + outPath;
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytecode.data()), static_cast<std::streamsize>(bytecode.size()));
        return static_cast<bool>(out);
    }

    bool JsScripting::Eval(const std::string& code, std::string* outResult) {
        State& s = S();
        if (!s.Context) {
            if (outResult) *outResult = "no script bundle loaded";
            return false;
        }
        JSValue result = JS_Eval(s.Context, code.c_str(), code.size(), "<eval>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(s.Context);
            if (outResult) *outResult = ToStdString(s.Context, exception);
            JS_FreeValue(s.Context, exception);
            return false;
        }
        if (outResult)
            *outResult = ToStdString(s.Context, result);
        JS_FreeValue(s.Context, result);
        DrainJobs();
        return true;
    }

    std::string JsScripting::RemapStack(const std::string& text) {
        State& s = S();
        if (s.BundleName.empty())
            return text;
        EnsureSourceMap();
        if (s.Map.Empty())
            return text;
        // Replace every "<bundle>:<line>[:<col>]" occurrence.
        std::string result;
        result.reserve(text.size() + 64);
        const std::string& name = s.BundleName;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t found = text.find(name + ":", pos);
            if (found == std::string::npos) {
                result.append(text, pos, std::string::npos);
                break;
            }
            result.append(text, pos, found - pos);
            size_t cursor = found + name.size() + 1;
            auto readNumber = [&](int& value) {
                size_t begin = cursor;
                value = 0;
                while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
                    value = value * 10 + (text[cursor++] - '0');
                return cursor > begin;
            };
            int line = 0, column = 0;
            if (!readNumber(line)) {
                result.append(text, found, cursor - found);
                pos = cursor;
                continue;
            }
            if (cursor < text.size() && text[cursor] == ':') {
                ++cursor;
                if (!readNumber(column))
                    column = 0;
            }
            std::string source;
            int sourceLine = 0, sourceColumn = 0;
            if (s.Map.Lookup(line, column, source, sourceLine, sourceColumn))
                result += source + ":" + std::to_string(sourceLine) + ":" + std::to_string(sourceColumn);
            else
                result.append(text, found, cursor - found);
            pos = cursor;
        }
        return result;
    }

    void JsScripting::OnSceneStopped(Scene* scene) {
        State& s = S();
        if (!scene)
            return;
        for (size_t i = 1; i < s.Scenes.size(); ++i) {
            if (s.Scenes[i] != scene)
                continue;
            if (s.Context && JS_IsFunction(s.Context, s.HookSceneStopped)) {
                JSValue arg = JS_NewInt32(s.Context, static_cast<int>(i));
                CallJs(s.HookSceneStopped, JS_UNDEFINED, 1, &arg, "scene stop");
            }
            s.Scenes[i] = nullptr; // never reused: a stale id can't alias a new scene
            break;
        }
        if (s.Runtime)
            JS_RunGC(s.Runtime);
    }

    // No dynamic_cast: the 3DS build may run without RTTI. Live TS instances are all in the
    // key table, so identity lookup there is exact (scratch Inspector instances never need it).
    static JsBehaviour* FindLive(const Behaviour* script) {
        if (!script)
            return nullptr;
        for (auto& [key, behaviour] : S().Behaviours)
            if (static_cast<const Behaviour*>(behaviour) == script)
                return behaviour;
        return nullptr;
    }

    void JsScripting::OnSceneFrameEnd(Scene* scene) {
        State& s = S();
        if (!s.Context || !scene || !JS_IsFunction(s.Context, s.HookTick))
            return;
        // Only scenes that ever ran a TS script have a slot; don't assign one just for this.
        for (size_t i = 1; i < s.Scenes.size(); ++i) {
            if (s.Scenes[i] != scene)
                continue;
            ScriptContext::Bind(ScriptContext::Services(), scene, 0);
            JSValue arg = JS_NewInt32(s.Context, static_cast<int>(i));
            CallJs(s.HookTick, JS_UNDEFINED, 1, &arg, "coroutines/timers");
            ScriptContext::Clear();
            return;
        }
    }

    bool JsScripting::CallMethod(Behaviour* script, const char* methodName) {
        JsBehaviour* behaviour = FindLive(script);
        return behaviour && behaviour->CallNamed(methodName);
    }

    bool JsScripting::IsTypeScriptInstance(const Behaviour* script) {
        return FindLive(script) != nullptr;
    }

}
