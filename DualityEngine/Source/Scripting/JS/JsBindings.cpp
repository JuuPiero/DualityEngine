// The `__duality` native bridge the TypeScript runtime (Tools/TypeScript/duality/duality.ts)
// is built on. Deliberately small, flat and allocation-free: entities travel as one double
// ("entity id" = scene slot * 2^32 + entt handle), vector results are written into a shared
// Float64Array (`scratch`) instead of returning new arrays, and component fields are reached
// through integer ids resolved once (`fid`). Everything else -- GameObject/Transform/Vector3
// classes, getComponent, decorators -- is plain TypeScript bundled into the game.

#include "JsInternal.h"

#include <algorithm>
#include <cmath>

#include <box2d/box2d.h>
#include <btBulletDynamicsCommon.h>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/ScriptableObjectLoader.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scripting/ScriptContext.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"
#include "DualityEngine/Scripting/ScriptableObjectRegistry.h"

namespace Duality::Js {

    namespace {

        // --- argument helpers ---------------------------------------------------------------
        double Num(JSContext* ctx, int argc, JSValueConst* argv, int i, double fallback = 0.0) {
            if (i >= argc)
                return fallback;
            double value = fallback;
            if (JS_ToFloat64(ctx, &value, argv[i]) != 0) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                return fallback;
            }
            return value;
        }
        float F(JSContext* ctx, int argc, JSValueConst* argv, int i, float fallback = 0.0f) {
            return static_cast<float>(Num(ctx, argc, argv, i, fallback));
        }
        int Int(JSContext* ctx, int argc, JSValueConst* argv, int i, int fallback = 0) {
            return static_cast<int>(Num(ctx, argc, argv, i, fallback));
        }
        bool Bool(JSContext* ctx, int argc, JSValueConst* argv, int i) {
            return i < argc && JS_ToBool(ctx, argv[i]) > 0;
        }
        std::string Str(JSContext* ctx, int argc, JSValueConst* argv, int i) {
            if (i >= argc)
                return {};
            const char* text = JS_ToCString(ctx, argv[i]);
            std::string result = text ? text : "";
            if (text)
                JS_FreeCString(ctx, text);
            return result;
        }
        Entity Ent(JSContext* ctx, int argc, JSValueConst* argv, int i) {
            return EntityFromId(Num(ctx, argc, argv, i, -1.0));
        }
        Scene* SceneArg(JSContext* ctx, int argc, JSValueConst* argv, int i) {
            return SceneFromSlot(Num(ctx, argc, argv, i, 0.0));
        }
        JSValue Id(JSContext* ctx, Entity entity) {
            return JS_NewFloat64(ctx, MakeEntityId(entity));
        }
        void Out(int index, double value) {
            State& s = S();
            if (s.Scratch && static_cast<size_t>(index) < s.ScratchCount)
                s.Scratch[index] = value;
        }
        void Out3(const glm::vec3& v, int offset = 0) { Out(offset, v.x); Out(offset + 1, v.y); Out(offset + 2, v.z); }

        const EngineServices* Services() { return ScriptContext::Services(); }

#define DUALITY_JS_FN(name) JSValue name(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
#define UNUSED_ARGS (void)argc; (void)argv

        // --- logging / time / input ----------------------------------------------------------
        DUALITY_JS_FN(Log_) {
            const int level = Int(ctx, argc, argv, 0);
            const std::string message = Str(ctx, argc, argv, 1);
            if (level >= 2) Log::Error(message);
            else if (level == 1) Log::Warn(message);
            else Log::Info(message);
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(Dt) {
            UNUSED_ARGS;
            if (S().InFixedUpdate)
                return JS_NewFloat64(ctx, 1.0 / 60.0);
            const EngineServices* services = Services();
            return JS_NewFloat64(ctx, services && services->GetDeltaTime ? services->GetDeltaTime() : 0.0);
        }
        DUALITY_JS_FN(Time_) {
            UNUSED_ARGS;
            const EngineServices* services = Services();
            return JS_NewFloat64(ctx, services && services->GetElapsedTime ? services->GetElapsedTime() : 0.0);
        }
        DUALITY_JS_FN(Frame) {
            UNUSED_ARGS;
            const EngineServices* services = Services();
            return JS_NewFloat64(ctx, services && services->GetFrameCount ? static_cast<double>(services->GetFrameCount()) : 0.0);
        }
        DUALITY_JS_FN(InFixed) { UNUSED_ARGS; return JS_NewBool(ctx, S().InFixedUpdate); }
        DUALITY_JS_FN(Realtime) { UNUSED_ARGS; return JS_NewFloat64(ctx, NowSeconds()); }

        DUALITY_JS_FN(Key) {
            const int code = Int(ctx, argc, argv, 0, -1);
            const int mode = Int(ctx, argc, argv, 1);
            if (code < 0 || code >= static_cast<int>(KeyCode::Count))
                return JS_FALSE;
            const KeyCode key = static_cast<KeyCode>(code);
            const bool result = mode == 1 ? InputManager::GetKeyDown(key) : mode == 2 ? InputManager::GetKeyUp(key) : InputManager::GetKey(key);
            return JS_NewBool(ctx, result);
        }
        DUALITY_JS_FN(Axis) { return JS_NewFloat64(ctx, InputManager::GetAxis(Str(ctx, argc, argv, 0))); }
        DUALITY_JS_FN(Pointer) {
            UNUSED_ARGS;
            glm::vec2 position = InputManager::GetPointerPosition();
            Out(0, position.x);
            Out(1, position.y);
            Out(2, static_cast<double>(static_cast<int>(InputManager::GetPointerScreen())));
            return JS_NewBool(ctx, InputManager::GetPointerDown());
        }
        DUALITY_JS_FN(PointerUp) { UNUSED_ARGS; return JS_NewBool(ctx, InputManager::GetPointerUp()); }
        DUALITY_JS_FN(PointerPressed) { UNUSED_ARGS; return JS_NewBool(ctx, InputManager::GetPointerPressed()); }

        // --- entities ----------------------------------------------------------------------------
        DUALITY_JS_FN(CurScene) {
            UNUSED_ARGS;
            return JS_NewInt32(ctx, static_cast<int>(SceneSlot(static_cast<Scene*>(ScriptContext::Scene()))));
        }
        DUALITY_JS_FN(Valid) { return JS_NewBool(ctx, static_cast<bool>(Ent(ctx, argc, argv, 0))); }
        DUALITY_JS_FN(GetName) {
            Entity e = Ent(ctx, argc, argv, 0);
            auto* name = e ? e.TryGetComponent<NameComponent>() : nullptr;
            return name ? JS_NewStringLen(ctx, name->Name.data(), name->Name.size()) : JS_NewString(ctx, "");
        }
        DUALITY_JS_FN(SetName) {
            Entity e = Ent(ctx, argc, argv, 0);
            if (auto* name = e ? e.TryGetComponent<NameComponent>() : nullptr)
                name->Name = Str(ctx, argc, argv, 1);
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(GetTag) {
            Entity e = Ent(ctx, argc, argv, 0);
            auto* tag = e ? e.TryGetComponent<TagComponent>() : nullptr;
            return tag ? JS_NewStringLen(ctx, tag->Tag.data(), tag->Tag.size()) : JS_NewString(ctx, "");
        }
        DUALITY_JS_FN(SetTag) {
            Entity e = Ent(ctx, argc, argv, 0);
            if (auto* tag = e ? e.TryGetComponent<TagComponent>() : nullptr)
                tag->Tag = Str(ctx, argc, argv, 1);
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(ActiveSelf) {
            Entity e = Ent(ctx, argc, argv, 0);
            auto* active = e ? e.TryGetComponent<ActiveComponent>() : nullptr;
            return JS_NewBool(ctx, active && active->Active);
        }
        DUALITY_JS_FN(SetActive) {
            Entity e = Ent(ctx, argc, argv, 0);
            if (auto* active = e ? e.TryGetComponent<ActiveComponent>() : nullptr)
                active->Active = Bool(ctx, argc, argv, 1);
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(ActiveInHierarchy) {
            Entity e = Ent(ctx, argc, argv, 0);
            return JS_NewBool(ctx, e && e.GetScene()->IsEffectivelyActive(e));
        }
        DUALITY_JS_FN(Parent) {
            Entity e = Ent(ctx, argc, argv, 0);
            return Id(ctx, e ? e.GetScene()->GetParent(e) : Entity{});
        }
        DUALITY_JS_FN(ChildCount) {
            Entity e = Ent(ctx, argc, argv, 0);
            auto* hierarchy = e ? e.TryGetComponent<HierarchyComponent>() : nullptr;
            return JS_NewInt32(ctx, hierarchy ? static_cast<int>(hierarchy->Children.size()) : 0);
        }
        DUALITY_JS_FN(Child) {
            Entity e = Ent(ctx, argc, argv, 0);
            const int index = Int(ctx, argc, argv, 1, -1);
            auto* hierarchy = e ? e.TryGetComponent<HierarchyComponent>() : nullptr;
            if (!hierarchy || index < 0 || index >= static_cast<int>(hierarchy->Children.size()))
                return JS_NewFloat64(ctx, -1.0);
            return Id(ctx, hierarchy->Children[index]);
        }
        DUALITY_JS_FN(SetParent) {
            Entity e = Ent(ctx, argc, argv, 0);
            Entity parent = Ent(ctx, argc, argv, 1);
            if (!e || (parent && parent.GetScene() != e.GetScene()))
                return JS_FALSE;
            e.GetScene()->SetParent(e, parent, Entity{}, argc > 2 ? Bool(ctx, argc, argv, 2) : true);
            return JS_TRUE;
        }
        DUALITY_JS_FN(Find) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            const std::string name = Str(ctx, argc, argv, 1);
            if (!scene)
                return JS_NewFloat64(ctx, -1.0);
            // Hierarchy order (roots first, then depth-first), like Unity's GameObject.Find.
            for (Entity entity : scene->GetHierarchyTraversalOrder()) {
                auto* component = entity.TryGetComponent<NameComponent>();
                if (component && component->Name == name)
                    return Id(ctx, entity);
            }
            return JS_NewFloat64(ctx, -1.0);
        }
        DUALITY_JS_FN(FindTag) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            const std::string tag = Str(ctx, argc, argv, 1);
            JSValue array = JS_NewArray(ctx);
            if (!scene)
                return array;
            uint32_t index = 0;
            for (Entity entity : scene->GetHierarchyTraversalOrder()) {
                auto* component = entity.TryGetComponent<TagComponent>();
                if (component && component->Tag == tag)
                    JS_SetPropertyUint32(ctx, array, index++, Id(ctx, entity));
            }
            return array;
        }
        DUALITY_JS_FN(Create) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            if (!scene)
                return JS_NewFloat64(ctx, -1.0);
            const std::string name = argc > 1 ? Str(ctx, argc, argv, 1) : std::string("GameObject");
            return Id(ctx, scene->CreateEntity(name.empty() ? "GameObject" : name));
        }
        DUALITY_JS_FN(Destroy) {
            Entity e = Ent(ctx, argc, argv, 0);
            if (e)
                e.GetScene()->DestroyEntityDeferred(e);
            return JS_UNDEFINED;
        }
        // Spawn position is applied BEFORE the physics body/scripts are created, so a dynamic
        // body starts where the script asked (setting the transform afterwards would not move it).
        void PlaceAt(Entity root, float x, float y, float z) {
            TransformComponent world = root.GetScene()->GetWorldTransform(root);
            world.Translation = glm::vec3(x, y, z);
            root.GetComponent<TransformComponent>().Translation = root.GetScene()->WorldToLocal(root, world).Translation;
        }
        DUALITY_JS_FN(Instantiate) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            const std::string guid = Str(ctx, argc, argv, 1);
            Entity parent = Ent(ctx, argc, argv, 2);
            if (!scene || guid.empty())
                return JS_NewFloat64(ctx, -1.0);
            const std::string path = AssetDatabase::ResolvePath(guid);
            if (path.empty()) {
                Log::Warn("[TS] instantiate: no prefab asset with guid " + guid);
                return JS_NewFloat64(ctx, -1.0);
            }
            if (parent && parent.GetScene() != scene)
                parent = Entity{};
            Entity root = PrefabSerializer::Instantiate(*scene, path, parent, AssetRef{ guid });
            if (!root)
                return JS_NewFloat64(ctx, -1.0);
            if (argc >= 6)
                PlaceAt(root, F(ctx, argc, argv, 3), F(ctx, argc, argv, 4), F(ctx, argc, argv, 5));
            scene->CreateRuntimeScripts(root);
            return Id(ctx, root);
        }
        DUALITY_JS_FN(Clone) {
            Entity source = Ent(ctx, argc, argv, 0);
            Entity parent = Ent(ctx, argc, argv, 1);
            if (!source)
                return JS_NewFloat64(ctx, -1.0);
            Scene* scene = source.GetScene();
            Entity copy = PrefabSerializer::Duplicate(*scene, source);
            if (!copy)
                return JS_NewFloat64(ctx, -1.0);
            if (parent && parent.GetScene() == scene)
                scene->SetParent(copy, parent, Entity{}, true);
            if (argc >= 5)
                PlaceAt(copy, F(ctx, argc, argv, 2), F(ctx, argc, argv, 3), F(ctx, argc, argv, 4));
            scene->CreateRuntimeScripts(copy);
            return Id(ctx, copy);
        }

        // --- transform -----------------------------------------------------------------------
        // which: 0 localPosition, 1 position (world), 2 localEulerAngles, 3 eulerAngles (world),
        //        4 localScale, 5 lossyScale (world, read-only)
        DUALITY_JS_FN(TGet) {
            Entity e = Ent(ctx, argc, argv, 0);
            const int which = Int(ctx, argc, argv, 1);
            auto* local = e ? e.TryGetComponent<TransformComponent>() : nullptr;
            if (!local)
                return JS_FALSE;
            if (which == 0) Out3(local->Translation);
            else if (which == 2) Out3(local->Rotation);
            else if (which == 4) Out3(local->Scale);
            else {
                TransformComponent world = e.GetScene()->GetWorldTransform(e);
                Out3(which == 1 ? world.Translation : which == 3 ? world.Rotation : world.Scale);
            }
            return JS_TRUE;
        }
        DUALITY_JS_FN(TSet) {
            Entity e = Ent(ctx, argc, argv, 0);
            const int which = Int(ctx, argc, argv, 1);
            auto* local = e ? e.TryGetComponent<TransformComponent>() : nullptr;
            if (!local)
                return JS_FALSE;
            const glm::vec3 v(F(ctx, argc, argv, 2), F(ctx, argc, argv, 3), F(ctx, argc, argv, 4));
            switch (which) {
                case 0: local->Translation = v; break;
                case 2: local->Rotation = v; break;
                case 4: local->Scale = v; break;
                case 1:
                case 3: {
                    TransformComponent world = e.GetScene()->GetWorldTransform(e);
                    if (which == 1) world.Translation = v; else world.Rotation = v;
                    TransformComponent newLocal = e.GetScene()->WorldToLocal(e, world);
                    if (which == 1) local->Translation = newLocal.Translation; else local->Rotation = newLocal.Rotation;
                    break;
                }
                default: return JS_FALSE;
            }
            return JS_TRUE;
        }
        DUALITY_JS_FN(Translate) {
            Entity e = Ent(ctx, argc, argv, 0);
            if (auto* local = e ? e.TryGetComponent<TransformComponent>() : nullptr)
                local->Translation += glm::vec3(F(ctx, argc, argv, 1), F(ctx, argc, argv, 2), F(ctx, argc, argv, 3));
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(Rotate) {
            Entity e = Ent(ctx, argc, argv, 0);
            if (auto* local = e ? e.TryGetComponent<TransformComponent>() : nullptr)
                local->Rotation += glm::vec3(F(ctx, argc, argv, 1), F(ctx, argc, argv, 2), F(ctx, argc, argv, 3));
            return JS_UNDEFINED;
        }

        // --- generic components (TypeRegistry) ------------------------------------------------
        struct FieldRef {
            ComponentTypeInfo* Type;
            const FieldHandle* Field;
        };
        std::vector<FieldRef>& FieldTable() {
            static std::vector<FieldRef> table;
            return table;
        }

        DUALITY_JS_FN(Has) {
            Entity e = Ent(ctx, argc, argv, 0);
            ComponentTypeInfo* type = TypeRegistry::Find(Str(ctx, argc, argv, 1));
            return JS_NewBool(ctx, e && type && type->Has(e));
        }
        DUALITY_JS_FN(Add) {
            Entity e = Ent(ctx, argc, argv, 0);
            ComponentTypeInfo* type = TypeRegistry::Find(Str(ctx, argc, argv, 1));
            if (!e || !type || type->DisplayName == "Scripts")
                return JS_FALSE;
            type->AddDefault(e);
            return JS_TRUE;
        }
        DUALITY_JS_FN(Remove) {
            Entity e = Ent(ctx, argc, argv, 0);
            ComponentTypeInfo* type = TypeRegistry::Find(Str(ctx, argc, argv, 1));
            if (!e || !type || type->Mandatory || type->DisplayName == "Scripts" || !type->Has(e))
                return JS_FALSE;
            type->Remove(e);
            return JS_TRUE;
        }
        DUALITY_JS_FN(FieldId) {
            const std::string typeName = Str(ctx, argc, argv, 0);
            const std::string fieldName = Str(ctx, argc, argv, 1);
            ComponentTypeInfo* type = TypeRegistry::Find(typeName);
            if (!type)
                return JS_NewInt32(ctx, -1);
            for (const FieldHandle& field : type->Fields) {
                if (field.Name != fieldName)
                    continue;
                auto& table = FieldTable();
                for (size_t i = 0; i < table.size(); ++i)
                    if (table[i].Field == &field)
                        return JS_NewInt32(ctx, static_cast<int>(i));
                table.push_back({ type, &field });
                return JS_NewInt32(ctx, static_cast<int>(table.size() - 1));
            }
            return JS_NewInt32(ctx, -1);
        }
        DUALITY_JS_FN(FieldGet) {
            Entity e = Ent(ctx, argc, argv, 0);
            const int id = Int(ctx, argc, argv, 1, -1);
            auto& table = FieldTable();
            if (!e || id < 0 || id >= static_cast<int>(table.size()) || !table[id].Type->Has(e))
                return JS_UNDEFINED;
            return FieldValueToJs(ctx, table[id].Field->Get(table[id].Type->GetPtr(e)), e.GetScene());
        }
        DUALITY_JS_FN(FieldSet) {
            Entity e = Ent(ctx, argc, argv, 0);
            const int id = Int(ctx, argc, argv, 1, -1);
            auto& table = FieldTable();
            if (!e || argc < 3 || id < 0 || id >= static_cast<int>(table.size()) || !table[id].Type->Has(e))
                return JS_FALSE;
            void* component = table[id].Type->GetPtr(e);
            FieldValue value;
            if (!JsToFieldValue(ctx, argv[2], table[id].Field->Get(component), e.GetScene(), value))
                return JS_FALSE;
            table[id].Field->Set(component, value);
            return JS_TRUE;
        }

        // --- script slots (C++ and TypeScript) on an entity ------------------------------------
        ScriptInstance* Slot(Entity e, int index) {
            auto* bc = e ? e.TryGetComponent<BehaviourComponent>() : nullptr;
            if (!bc || index < 0 || index >= static_cast<int>(bc->Scripts.size()))
                return nullptr;
            return &bc->Scripts[index];
        }
        JsBehaviour* LiveJs(ScriptInstance* slot) {
            if (!slot || !slot->Instance)
                return nullptr;
            for (auto& [key, behaviour] : S().Behaviours)
                if (static_cast<Behaviour*>(behaviour) == slot->Instance)
                    return behaviour->IsLive() ? behaviour : nullptr;
            return nullptr;
        }
        const FieldHandle* ScriptField(ScriptInstance* slot, const std::string& name) {
            if (!slot)
                return nullptr;
            for (const FieldHandle& field : ScriptRegistry::GetFields(slot->ClassName))
                if (field.Name == name)
                    return &field;
            return nullptr;
        }

        DUALITY_JS_FN(ScriptCount) {
            Entity e = Ent(ctx, argc, argv, 0);
            auto* bc = e ? e.TryGetComponent<BehaviourComponent>() : nullptr;
            return JS_NewInt32(ctx, bc ? static_cast<int>(bc->Scripts.size()) : 0);
        }
        DUALITY_JS_FN(ScriptName) {
            ScriptInstance* slot = Slot(Ent(ctx, argc, argv, 0), Int(ctx, argc, argv, 1, -1));
            return slot ? JS_NewStringLen(ctx, slot->ClassName.data(), slot->ClassName.size()) : JS_NULL;
        }
        DUALITY_JS_FN(ScriptObject) {
            JsBehaviour* behaviour = LiveJs(Slot(Ent(ctx, argc, argv, 0), Int(ctx, argc, argv, 1, -1)));
            return behaviour ? JS_DupValue(ctx, behaviour->Object()) : JS_NULL;
        }
        DUALITY_JS_FN(ScriptLive) {
            ScriptInstance* slot = Slot(Ent(ctx, argc, argv, 0), Int(ctx, argc, argv, 1, -1));
            return JS_NewBool(ctx, slot && slot->Instance);
        }
        DUALITY_JS_FN(ScriptEnabled) {
            ScriptInstance* slot = Slot(Ent(ctx, argc, argv, 0), Int(ctx, argc, argv, 1, -1));
            return JS_NewBool(ctx, slot && slot->Enabled);
        }
        DUALITY_JS_FN(SetScriptEnabled) {
            if (ScriptInstance* slot = Slot(Ent(ctx, argc, argv, 0), Int(ctx, argc, argv, 1, -1)))
                slot->Enabled = Bool(ctx, argc, argv, 2);
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(ScriptGet) {
            Entity e = Ent(ctx, argc, argv, 0);
            ScriptInstance* slot = Slot(e, Int(ctx, argc, argv, 1, -1));
            const FieldHandle* field = ScriptField(slot, Str(ctx, argc, argv, 2));
            if (!field)
                return JS_UNDEFINED;
            if (slot->Instance)
                return FieldValueToJs(ctx, field->Get(slot->Instance), e.GetScene());
            auto it = slot->PropertyOverrides.find(field->Name);
            return it != slot->PropertyOverrides.end() ? FieldValueToJs(ctx, it->second, e.GetScene()) : JS_UNDEFINED;
        }
        DUALITY_JS_FN(ScriptSet) {
            Entity e = Ent(ctx, argc, argv, 0);
            ScriptInstance* slot = Slot(e, Int(ctx, argc, argv, 1, -1));
            const FieldHandle* field = ScriptField(slot, Str(ctx, argc, argv, 2));
            if (!field || !slot->Instance || argc < 4)
                return JS_FALSE;
            FieldValue value;
            if (!JsToFieldValue(ctx, argv[3], field->Get(slot->Instance), e.GetScene(), value))
                return JS_FALSE;
            field->Set(slot->Instance, value);
            return JS_TRUE;
        }
        DUALITY_JS_FN(ScriptFields) {
            ScriptInstance* slot = Slot(Ent(ctx, argc, argv, 0), Int(ctx, argc, argv, 1, -1));
            JSValue array = JS_NewArray(ctx);
            if (!slot)
                return array;
            uint32_t index = 0;
            for (const FieldHandle& field : ScriptRegistry::GetFields(slot->ClassName))
                JS_SetPropertyUint32(ctx, array, index++, JS_NewStringLen(ctx, field.Name.data(), field.Name.size()));
            return array;
        }
        DUALITY_JS_FN(AddScript) {
            Entity e = Ent(ctx, argc, argv, 0);
            const std::string className = Str(ctx, argc, argv, 1);
            if (!e || !ScriptRegistry::IsRegistered(className)) {
                Log::Warn("[TS] addComponent: unknown script class '" + className + "'");
                return JS_NewInt32(ctx, -1);
            }
            e.GetScene()->AddRuntimeScript(e, className);
            auto* bc = e.TryGetComponent<BehaviourComponent>();
            return JS_NewInt32(ctx, bc ? static_cast<int>(bc->Scripts.size()) - 1 : -1);
        }
        DUALITY_JS_FN(ScriptCall) {
            ScriptInstance* slot = Slot(Ent(ctx, argc, argv, 0), Int(ctx, argc, argv, 1, -1));
            JsBehaviour* behaviour = LiveJs(slot);
            const std::string method = Str(ctx, argc, argv, 2);
            return JS_NewBool(ctx, behaviour && behaviour->CallNamed(method.c_str()));
        }

        // `this.enabled` of a TS script, by its key.
        DUALITY_JS_FN(BehaviourEnabled) {
            JsBehaviour* behaviour = BehaviourFromKey(static_cast<uint32_t>(Num(ctx, argc, argv, 0)));
            return JS_NewBool(ctx, behaviour && behaviour->IsEnabled());
        }
        DUALITY_JS_FN(BehaviourSetEnabled) {
            if (JsBehaviour* behaviour = BehaviourFromKey(static_cast<uint32_t>(Num(ctx, argc, argv, 0))))
                behaviour->SetEnabled(Bool(ctx, argc, argv, 1));
            return JS_UNDEFINED;
        }

        // --- physics -------------------------------------------------------------------------------
        b2Body* Body2D(Entity e) {
            auto* rb = e ? e.TryGetComponent<Rigidbody2DComponent>() : nullptr;
            return rb ? static_cast<b2Body*>(rb->RuntimeBody) : nullptr;
        }
        btRigidBody* Body3D(Entity e) {
            auto* rb = e ? e.TryGetComponent<Rigidbody3DComponent>() : nullptr;
            return rb ? static_cast<btRigidBody*>(rb->RuntimeBody) : nullptr;
        }

        // rb2(id, op, x, y): 0 velocity->scratch, 1 set velocity, 2 addForce, 3 addForce(Impulse),
        // 4 angularVelocity (deg/s), 5 set angularVelocity, 6 addTorque, 7 addTorque(Impulse),
        // 8 mass, 9 movePosition, 10 isAwake, 11 wakeUp
        DUALITY_JS_FN(Rb2) {
            b2Body* body = Body2D(Ent(ctx, argc, argv, 0));
            const int op = Int(ctx, argc, argv, 1);
            if (!body)
                return op == 0 ? JS_FALSE : JS_NewFloat64(ctx, 0.0);
            const float x = F(ctx, argc, argv, 2), y = F(ctx, argc, argv, 3);
            const b2Vec2 physics(PhysicsUnits::ToPhysics(x), PhysicsUnits::ToPhysics(y));
            switch (op) {
                case 0: {
                    const b2Vec2& v = body->GetLinearVelocity();
                    Out(0, PhysicsUnits::ToWorld(v.x));
                    Out(1, PhysicsUnits::ToWorld(v.y));
                    return JS_TRUE;
                }
                case 1: body->SetLinearVelocity(physics); body->SetAwake(true); break;
                case 2: body->ApplyForceToCenter(physics, true); break;
                case 3: body->ApplyLinearImpulseToCenter(physics, true); break;
                case 4: return JS_NewFloat64(ctx, glm::degrees(body->GetAngularVelocity()));
                case 5: body->SetAngularVelocity(glm::radians(x)); body->SetAwake(true); break;
                case 6: body->ApplyTorque(x, true); break;
                case 7: body->ApplyAngularImpulse(x, true); break;
                case 8: return JS_NewFloat64(ctx, body->GetMass());
                case 9: body->SetTransform(physics, body->GetAngle()); body->SetAwake(true); break;
                case 10: return JS_NewBool(ctx, body->IsAwake());
                case 11: body->SetAwake(true); break;
                default: break;
            }
            return JS_UNDEFINED;
        }

        // rb3(id, op, x, y, z): 0 velocity->scratch, 1 set velocity, 2 addForce, 3 addForce(Impulse),
        // 4 angularVelocity (deg/s)->scratch, 5 set angularVelocity (deg/s), 6 addTorque,
        // 7 addTorque(Impulse), 8 mass
        DUALITY_JS_FN(Rb3) {
            btRigidBody* body = Body3D(Ent(ctx, argc, argv, 0));
            const int op = Int(ctx, argc, argv, 1);
            if (!body)
                return (op == 0 || op == 4) ? JS_FALSE : JS_NewFloat64(ctx, 0.0);
            const float x = F(ctx, argc, argv, 2), y = F(ctx, argc, argv, 3), z = F(ctx, argc, argv, 4);
            const btVector3 physics(PhysicsUnits::ToPhysics(x), PhysicsUnits::ToPhysics(y), PhysicsUnits::ToPhysics(z));
            switch (op) {
                case 0: {
                    const btVector3& v = body->getLinearVelocity();
                    Out(0, PhysicsUnits::ToWorld(v.x())); Out(1, PhysicsUnits::ToWorld(v.y())); Out(2, PhysicsUnits::ToWorld(v.z()));
                    return JS_TRUE;
                }
                case 1: body->setLinearVelocity(physics); body->activate(true); break;
                case 2: body->applyCentralForce(physics); body->activate(true); break;
                case 3: body->applyCentralImpulse(physics); body->activate(true); break;
                case 4: {
                    const btVector3& w = body->getAngularVelocity();
                    Out(0, glm::degrees(w.x())); Out(1, glm::degrees(w.y())); Out(2, glm::degrees(w.z()));
                    return JS_TRUE;
                }
                case 5: body->setAngularVelocity(btVector3(glm::radians(x), glm::radians(y), glm::radians(z))); body->activate(true); break;
                case 6: body->applyTorque(btVector3(x, y, z)); body->activate(true); break;
                case 7: body->applyTorqueImpulse(btVector3(x, y, z)); body->activate(true); break;
                case 8: {
                    const btScalar inverse = body->getInvMass();
                    return JS_NewFloat64(ctx, inverse > 0 ? 1.0 / inverse : 0.0);
                }
                default: break;
            }
            return JS_UNDEFINED;
        }

        DUALITY_JS_FN(Ray2) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            if (!scene)
                return JS_NewFloat64(ctx, -1.0);
            RaycastHit2D hit = scene->Raycast2D({ F(ctx, argc, argv, 1), F(ctx, argc, argv, 2) },
                { F(ctx, argc, argv, 3), F(ctx, argc, argv, 4) }, F(ctx, argc, argv, 5, 1e9f));
            if (!hit)
                return JS_NewFloat64(ctx, -1.0);
            Out(0, hit.Point.x); Out(1, hit.Point.y); Out(2, hit.Normal.x); Out(3, hit.Normal.y); Out(4, hit.Distance);
            return Id(ctx, hit.HitEntity);
        }
        DUALITY_JS_FN(Ray3) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            if (!scene)
                return JS_NewFloat64(ctx, -1.0);
            RaycastHit3D hit = scene->Raycast3D({ F(ctx, argc, argv, 1), F(ctx, argc, argv, 2), F(ctx, argc, argv, 3) },
                { F(ctx, argc, argv, 4), F(ctx, argc, argv, 5), F(ctx, argc, argv, 6) }, F(ctx, argc, argv, 7, 1e9f));
            if (!hit)
                return JS_NewFloat64(ctx, -1.0);
            Out3(hit.Point, 0); Out3(hit.Normal, 3); Out(6, hit.Distance);
            return Id(ctx, hit.HitEntity);
        }
        DUALITY_JS_FN(Point2) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            if (!scene)
                return JS_NewFloat64(ctx, -1.0);
            RaycastHit2D hit = scene->RaycastPoint2D({ F(ctx, argc, argv, 1), F(ctx, argc, argv, 2) });
            return hit ? Id(ctx, hit.HitEntity) : JS_NewFloat64(ctx, -1.0);
        }
        DUALITY_JS_FN(ScreenRay) {
            Scene* scene = SceneArg(ctx, argc, argv, 0);
            glm::vec3 origin, direction;
            if (!scene || !scene->ScreenPointToRay3D(Int(ctx, argc, argv, 1) == 1 ? Screen::Bottom : Screen::Top,
                    { F(ctx, argc, argv, 2), F(ctx, argc, argv, 3) }, origin, direction))
                return JS_FALSE;
            Out3(origin, 0); Out3(direction, 3);
            return JS_TRUE;
        }

        // --- audio -----------------------------------------------------------------------------------
        // audio(id, op, value): 0 play, 1 stop, 2 pause, 3 unpause, 4 setVolume, 5 isPlaying
        DUALITY_JS_FN(Audio) {
            Entity e = Ent(ctx, argc, argv, 0);
            const int op = Int(ctx, argc, argv, 1);
            const EngineServices* services = Services();
            if (!e || !services)
                return JS_FALSE;
            void* scene = e.GetScene();
            const unsigned int handle = static_cast<unsigned int>(e.Handle());
            switch (op) {
                case 0: services->AudioSourcePlay(scene, handle); break;
                case 1: services->AudioSourceStop(scene, handle); break;
                case 2: services->AudioSourceSetPaused(scene, handle, true); break;
                case 3: services->AudioSourceSetPaused(scene, handle, false); break;
                case 4: services->AudioSourceSetVolume(scene, handle, F(ctx, argc, argv, 2)); break;
                case 5: return JS_NewBool(ctx, services->AudioSourceIsPlaying(scene, handle));
                default: break;
            }
            return JS_TRUE;
        }
        DUALITY_JS_FN(PlaySound) {
            const EngineServices* services = Services();
            const std::string guid = Str(ctx, argc, argv, 0);
            if (services && !guid.empty())
                services->PlaySound(guid.c_str(), Bool(ctx, argc, argv, 1));
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(StopSounds) {
            UNUSED_ARGS;
            if (const EngineServices* services = Services())
                services->StopAllSounds();
            return JS_UNDEFINED;
        }

        // --- scenes / assets -----------------------------------------------------------------------
        DUALITY_JS_FN(LoadScene) {
            SceneManager::RequestLoadScene(Str(ctx, argc, argv, 0), Int(ctx, argc, argv, 1) == 1 ? LoadSceneMode::Additive : LoadSceneMode::Single);
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(UnloadScene) {
            SceneManager::RequestUnloadScene(Str(ctx, argc, argv, 0));
            return JS_UNDEFINED;
        }
        DUALITY_JS_FN(AssetPath) {
            const std::string path = AssetDatabase::ResolvePath(Str(ctx, argc, argv, 0));
            return JS_NewStringLen(ctx, path.data(), path.size());
        }

        // ScriptableObject assets (C++ classes from GameScripts): fields by name.
        bool LoadSo(const std::string& guid, ScriptableObjectLoader::Loaded& out) {
            const std::string path = AssetDatabase::ResolvePath(guid);
            if (path.empty())
                return false;
            out = ScriptableObjectLoader::Load(path);
            return out.Instance != nullptr;
        }
        const FieldHandle* SoField(const ScriptableObjectLoader::Loaded& loaded, const std::string& name) {
            const ScriptableObjectFactoryEntry* entry = ScriptableObjectRegistry::Find(loaded.ClassName);
            if (!entry)
                return nullptr;
            for (const FieldHandle& field : entry->Fields)
                if (field.Name == name)
                    return &field;
            return nullptr;
        }
        DUALITY_JS_FN(SoInfo) {
            ScriptableObjectLoader::Loaded loaded;
            if (!LoadSo(Str(ctx, argc, argv, 0), loaded))
                return JS_NULL;
            JSValue array = JS_NewArray(ctx);
            JS_SetPropertyUint32(ctx, array, 0, JS_NewStringLen(ctx, loaded.ClassName.data(), loaded.ClassName.size()));
            uint32_t index = 1;
            if (const ScriptableObjectFactoryEntry* entry = ScriptableObjectRegistry::Find(loaded.ClassName))
                for (const FieldHandle& field : entry->Fields)
                    JS_SetPropertyUint32(ctx, array, index++, JS_NewStringLen(ctx, field.Name.data(), field.Name.size()));
            return array;
        }
        DUALITY_JS_FN(SoGet) {
            ScriptableObjectLoader::Loaded loaded;
            if (!LoadSo(Str(ctx, argc, argv, 0), loaded))
                return JS_UNDEFINED;
            const FieldHandle* field = SoField(loaded, Str(ctx, argc, argv, 1));
            return field ? FieldValueToJs(ctx, field->Get(loaded.Instance), static_cast<Scene*>(ScriptContext::Scene())) : JS_UNDEFINED;
        }
        DUALITY_JS_FN(SoSet) {
            ScriptableObjectLoader::Loaded loaded;
            if (argc < 3 || !LoadSo(Str(ctx, argc, argv, 0), loaded))
                return JS_FALSE;
            const FieldHandle* field = SoField(loaded, Str(ctx, argc, argv, 1));
            if (!field)
                return JS_FALSE;
            FieldValue value;
            if (!JsToFieldValue(ctx, argv[2], field->Get(loaded.Instance), static_cast<Scene*>(ScriptContext::Scene()), value))
                return JS_FALSE;
            field->Set(loaded.Instance, value);
            return JS_TRUE;
        }

        DUALITY_JS_FN(Platform) {
            UNUSED_ARGS;
#if defined(DE_PLATFORM_3DS)
            return JS_NewString(ctx, "3ds");
#else
            return JS_NewString(ctx, "desktop");
#endif
        }
        DUALITY_JS_FN(Memory) {
            UNUSED_ARGS;
            JsMemoryStats stats = JsScripting::GetMemoryStats();
            Out(0, static_cast<double>(stats.MallocSize));
            Out(1, static_cast<double>(stats.MallocLimit));
            Out(2, static_cast<double>(stats.ObjectCount));
            return JS_TRUE;
        }
        DUALITY_JS_FN(Gc) {
            UNUSED_ARGS;
            JsScripting::RunGC();
            return JS_UNDEFINED;
        }

#undef DUALITY_JS_FN
#undef UNUSED_ARGS

        struct NativeEntry {
            const char* Name;
            JSCFunction* Function;
            int Length;
        };

        const NativeEntry kNatives[] = {
            { "log", Log_, 2 }, { "dt", Dt, 0 }, { "time", Time_, 0 }, { "frame", Frame, 0 },
            { "inFixed", InFixed, 0 }, { "realtime", Realtime, 0 },
            { "key", Key, 2 }, { "axis", Axis, 1 }, { "ptr", Pointer, 0 }, { "ptrUp", PointerUp, 0 }, { "ptrPressed", PointerPressed, 0 },
            { "curScene", CurScene, 0 }, { "valid", Valid, 1 },
            { "getName", GetName, 1 }, { "setName", SetName, 2 }, { "getTag", GetTag, 1 }, { "setTag", SetTag, 2 },
            { "activeSelf", ActiveSelf, 1 }, { "setActive", SetActive, 2 }, { "activeInHierarchy", ActiveInHierarchy, 1 },
            { "parent", Parent, 1 }, { "childCount", ChildCount, 1 }, { "child", Child, 2 }, { "setParent", SetParent, 3 },
            { "find", Find, 2 }, { "findTag", FindTag, 2 }, { "create", Create, 2 }, { "destroy", Destroy, 1 },
            { "instantiate", Instantiate, 3 }, { "clone", Clone, 2 },
            { "tget", TGet, 2 }, { "tset", TSet, 5 }, { "translate", Translate, 4 }, { "rotate", Rotate, 4 },
            { "has", Has, 2 }, { "add", Add, 2 }, { "remove", Remove, 2 },
            { "fid", FieldId, 2 }, { "fget", FieldGet, 2 }, { "fset", FieldSet, 3 },
            { "scriptCount", ScriptCount, 1 }, { "scriptName", ScriptName, 2 }, { "scriptObject", ScriptObject, 2 },
            { "scriptLive", ScriptLive, 2 }, { "scriptEnabled", ScriptEnabled, 2 }, { "setScriptEnabled", SetScriptEnabled, 3 },
            { "scriptGet", ScriptGet, 3 }, { "scriptSet", ScriptSet, 4 }, { "scriptFields", ScriptFields, 2 },
            { "addScript", AddScript, 2 }, { "scriptCall", ScriptCall, 3 },
            { "benabled", BehaviourEnabled, 1 }, { "bsetEnabled", BehaviourSetEnabled, 2 },
            { "rb2", Rb2, 4 }, { "rb3", Rb3, 5 }, { "ray2", Ray2, 6 }, { "ray3", Ray3, 8 }, { "point2", Point2, 3 },
            { "screenRay", ScreenRay, 4 },
            { "audio", Audio, 3 }, { "playSound", PlaySound, 2 }, { "stopSounds", StopSounds, 0 },
            { "loadScene", LoadScene, 2 }, { "unloadScene", UnloadScene, 1 }, { "assetPath", AssetPath, 1 },
            { "soInfo", SoInfo, 1 }, { "soGet", SoGet, 2 }, { "soSet", SoSet, 3 },
            { "platform", Platform, 0 }, { "memory", Memory, 0 }, { "gc", Gc, 0 },
        };
    }

    void InstallBindings(JSContext* ctx) {
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue bridge = JS_NewObject(ctx);
        for (const NativeEntry& entry : kNatives)
            JS_SetPropertyStr(ctx, bridge, entry.Name, JS_NewCFunction(ctx, entry.Function, entry.Name, entry.Length));
        InstallRuntimeNatives(ctx, bridge);
        JS_SetPropertyStr(ctx, global, "__duality", bridge); // takes ownership of `bridge`
        JS_FreeValue(ctx, global);
    }

}
