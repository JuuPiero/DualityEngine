#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Physics/BodyType.h"
#include "DualityEngine/Renderer/MeshPrimitive.h"
#include "DualityEngine/Renderer/ProjectionType.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Renderer/UIAnchor.h"

namespace Duality {

    // Distinct wrapper so a glm::vec4 field can be tagged "this is a color"
    // (ColorEdit4 widget, no [0,1] clamp assumption elsewhere) without a
    // separate enum tag -- the value's own type in the FieldValue variant
    // *is* the dispatch key, for both the Properties panel and
    // serialization.
    struct Color4 { glm::vec4 Value; };

    // A reference to an asset by its stable `.meta` GUID (see
    // DualityEngine/Asset/AssetMeta.h/AssetDatabase.h) rather than a raw
    // path, so a field survives the asset being renamed/moved. Empty Guid
    // means "no asset assigned". Usable directly with the generic
    // MakeField<C,T>() template like any other field type -- no special
    // construction needed, unlike Color4.
    struct AssetRef { std::string Guid; };

    // A reference to another entity in the same Scene, by its raw entt handle (kept as a
    // plain uint32_t rather than entt::entity so this header doesn't need <entt.hpp> --
    // Behaviour::ResolveEntityRef/MakeEntityRef do the entt::entity cast). Invalid = "no
    // entity assigned". Unlike AssetRef, this is NOT stable across a scene save/load: a
    // reloaded scene's entities get freshly assigned handles in creation order, so a raw
    // handle from a previous session would silently resolve to the wrong entity (or none) --
    // FieldValueToJson/JsonToFieldValue deliberately never round-trip this value, always
    // saving/restoring it as "unset" instead. Usable directly with MakeField<C,T>() like any
    // other field type.
    struct EntityRef {
        static constexpr uint32_t Invalid = 0xFFFFFFFF;
        uint32_t Handle = Invalid;
    };

    // A nested DUALITY_SERIALIZABLE() struct's own fields, snapshotted by VALUE -- Unity's
    // [System.Serializable] class-as-a-field, for this engine (see MakeNestedField below).
    // Deliberately value-semantic, NOT a pointer into the owning instance: FieldValue is used as
    // a real snapshot that outlives any one synchronous call in practice (ScriptInstance::
    // PropertyOverrides, a persistent map -- PropertiesPanel.cpp's DrawScriptFields creates a
    // THROWAWAY scratch Behaviour every ImGui frame, seeds/draws/captures-back into that map,
    // then destroys the scratch before the next frame). A pointer captured here would dangle the
    // instant that scratch is destroyed, and silently fail to apply onto a freshly-created
    // instance at Play start -- confirmed as a real design flaw during review before this value-
    // semantic version was chosen instead, matching the copy-out/mutate/copy-back convention
    // every other FieldValue alternative already uses.
    //
    // Both members are std::any, not their "real" types directly -- NestedFieldValue must be
    // declared BEFORE FieldValue's own variant (it's one of its alternatives) and BEFORE
    // FieldHandle is a complete type (FieldHandle itself holds a FieldValue by value), so
    // neither `std::vector<FieldValue>` (Values' real type) nor `std::vector<FieldHandle> (*)()`
    // (FieldsFn's real type) can be named directly at this point without a forward-declaration
    // ordering problem. std::any sidesteps both -- it's fully independent of whatever type it
    // holds, recovered via std::any_cast at each real use site (MakeNestedField,
    // FieldEditorWidget.cpp's DrawFieldValueWidget Nested branch, FieldSerialization.cpp's
    // FieldValueToJson/JsonToFieldValue Nested branches).
    struct NestedFieldValue {
        std::any Values;   // actually std::vector<FieldValue>, one per FieldsFn()'s own entry, same order
        std::any FieldsFn; // actually std::vector<FieldHandle> (*)() -- the nested type's own static Fields()
    };

    // std::vector<AssetRef> and NestedFieldValue are the two compound alternatives here (the
    // former added for MeshRendererComponent::Materials, the latter for DUALITY_SERIALIZABLE()
    // nested structs -- see their own comments) -- deliberately not a generic "any field type/
    // any struct" mechanism, just these two concrete cases. Each real std::visit dispatch site
    // (FieldEditorWidget.cpp's DrawFieldValueWidget, FieldSerialization.cpp's
    // FieldValueToJson/JsonToFieldValue) needs exactly one new `if constexpr` branch per case --
    // TypeRegistry/MakeField/EntitySerialization.cpp need no changes at all, already fully
    // generic over whatever alternatives this variant holds.
    using FieldValue = std::variant<int, float, bool, std::string, glm::vec2, glm::vec3, glm::vec4, Color4, Screen, AssetRef, ProjectionType, MeshPrimitive, UIAnchor, BodyType, EntityRef, std::vector<AssetRef>, NestedFieldValue>;

    // A named, type-erased accessor for one field of a component/script
    // instance. Reflection is only ever walked from the Properties panel
    // and (de)serialization -- never from a per-frame hot path -- so the
    // std::function indirection here is a non-issue.
    struct FieldHandle {
        std::string Name;
        std::function<FieldValue(void*)> Get;
        std::function<void(void*, const FieldValue&)> Set;
    };

    template<typename C, typename T>
    FieldHandle MakeField(const std::string& name, T C::* member) {
        FieldHandle handle;
        handle.Name = name;
        handle.Get = [member](void* instance) -> FieldValue {
            return FieldValue(static_cast<C*>(instance)->*member);
        };
        handle.Set = [member](void* instance, const FieldValue& value) {
            static_cast<C*>(instance)->*member = std::get<T>(value);
        };
        return handle;
    }

    // Same as MakeField, but for a glm::vec4 member that should be treated
    // (and edited/serialized) as a color rather than a generic 4-vector.
    template<typename C>
    FieldHandle MakeColorField(const std::string& name, glm::vec4 C::* member) {
        FieldHandle handle;
        handle.Name = name;
        handle.Get = [member](void* instance) -> FieldValue {
            return FieldValue(Color4{ static_cast<C*>(instance)->*member });
        };
        handle.Set = [member](void* instance, const FieldValue& value) {
            static_cast<C*>(instance)->*member = std::get<Color4>(value).Value;
        };
        return handle;
    }

    // For a field whose TYPE is itself a DUALITY_SERIALIZABLE() struct (T::Fields() must exist)
    // -- see NestedFieldValue's own comment for why this snapshots by value instead of holding a
    // pointer into the live instance. Get walks T::Fields() once, reading each inner field's
    // current value off the live nested member; Set walks T::Fields() again, writing each
    // snapshotted value back onto the live nested member. Mismatched value/field counts (should
    // never happen in practice -- both always come from the same T::Fields() call shape) are
    // handled by simply not touching whichever tail doesn't have a counterpart, rather than
    // asserting -- consistent with every other FieldValue accessor's graceful-degradation
    // convention in this codebase.
    template<typename C, typename T>
    FieldHandle MakeNestedField(const std::string& name, T C::* member) {
        FieldHandle handle;
        handle.Name = name;
        handle.Get = [member](void* instance) -> FieldValue {
            T& nested = static_cast<C*>(instance)->*member;
            std::vector<FieldHandle> fields = T::Fields();
            std::vector<FieldValue> values;
            values.reserve(fields.size());
            for (FieldHandle& field : fields)
                values.push_back(field.Get(&nested));
            return FieldValue(NestedFieldValue{ std::any(std::move(values)), std::any(&T::Fields) });
        };
        handle.Set = [member](void* instance, const FieldValue& value) {
            T& nested = static_cast<C*>(instance)->*member;
            const auto& values = std::any_cast<const std::vector<FieldValue>&>(std::get<NestedFieldValue>(value).Values);
            std::vector<FieldHandle> fields = T::Fields();
            for (size_t i = 0; i < fields.size() && i < values.size(); i++)
                fields[i].Set(&nested, values[i]);
        };
        return handle;
    }

}
