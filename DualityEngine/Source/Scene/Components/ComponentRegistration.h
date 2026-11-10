#pragma once

// Internal helpers for the per-domain component registration files in this folder
// (CoreComponents.cpp, Rendering2DComponents.cpp, ...). Each file registers its components with
// TypeRegistry in one place: serialized name, fields (the serialization contract -- scenes,
// prefabs, copy/paste, undo and TypeScript `get('Field')` all go through them), Inspector hints
// (range / tooltip / header / asset type) and the Add Component category.

#include <string>
#include <utility>
#include <vector>

#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality::ComponentRegistration {

    // ---- Registration ------------------------------------------------------------------------

    // A component shown in Add Component / the Component menu under `category`
    // (TypeRegistry.h's ComponentCategory names). Returns the entry for optional extras
    // (a custom AddDefault, HeaderComponent...).
    template<typename T>
    ComponentTypeInfo& Component(const char* displayName, const char* category, std::vector<FieldHandle> fields) {
        ComponentTypeInfo& info = TypeRegistry::Register<T>(displayName, false, std::move(fields));
        info.Category = category;
        return info;
    }

    // Present on every entity (added by Scene::CreateEntity), never in Add Component.
    template<typename T>
    ComponentTypeInfo& Mandatory(const char* displayName, std::vector<FieldHandle> fields) {
        return TypeRegistry::Register<T>(displayName, true, std::move(fields));
    }

    // Registered (serialized, inspected) but not offered by Add Component.
    template<typename T>
    ComponentTypeInfo& Hidden(const char* displayName, std::vector<FieldHandle> fields) {
        return TypeRegistry::Register<T>(displayName, false, std::move(fields));
    }

    // ---- Inspector presentation (Unity's [Range] / [Tooltip] / [Header]) ---------------------

    inline FieldHandle Ranged(FieldHandle field, float min, float max, const char* tooltip = "") {
        field.HasRange = true;
        field.RangeMin = min;
        field.RangeMax = max;
        field.Tooltip = tooltip;
        return field;
    }

    inline FieldHandle Tip(FieldHandle field, const char* tooltip) {
        field.Tooltip = tooltip;
        return field;
    }

    inline FieldHandle Header(FieldHandle field, const char* header) {
        field.Header = header;
        return field;
    }

    // An AssetRef field whose object picker lists meshes (built-in Cube/Sphere/... + imported).
    inline FieldHandle MeshAsset(FieldHandle field) {
        field.AssetType = "Mesh";
        return field;
    }

    // A field declared on a common base struct (Joint2DBase/Joint3DBase) but registered for the
    // derived component type, so the void* handed to Get/Set is a Derived*.
    template<typename D, typename B, typename T>
    FieldHandle BaseField(const std::string& name, T B::* member) {
        FieldHandle handle;
        handle.Name = name;
        handle.Get = [member](void* instance) -> FieldValue {
            return FieldValue(static_cast<B&>(*static_cast<D*>(instance)).*member);
        };
        handle.Set = [member](void* instance, const FieldValue& value) {
            static_cast<B&>(*static_cast<D*>(instance)).*member = std::get<T>(value);
        };
        return handle;
    }

    inline void Append(std::vector<FieldHandle>& fields, std::vector<FieldHandle> more) {
        for (auto& field : more)
            fields.push_back(std::move(field));
    }

    // ---- Per-domain registration (called in this order by RegisterBuiltinComponents) -----------
    // The order is the Inspector's component card order and the Add Component list order.
    void RegisterCoreComponents();
    void RegisterRendering2DComponents();
    void RegisterAnimationComponents();
    void RegisterRendering3DComponents();
    void RegisterAudioComponents();
    void RegisterUIComponents();
    void RegisterPhysics2DComponents();
    void RegisterPhysics3DComponents();
    void RegisterConstraintComponents();
    void RegisterScriptingComponents();

}
