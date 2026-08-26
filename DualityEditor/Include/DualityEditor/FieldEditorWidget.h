#pragma once

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    class Scene;

    // Draws one ImGui widget for `field`, reading/writing it on `instance` (a component on an
    // Entity, or a ScriptableObject instance -- FieldHandle::Get/Set take a plain void*, so
    // either works identically). Extracted out of PropertiesPanel.cpp once a second consumer
    // (the ScriptableObject asset inspector, also in PropertiesPanel.cpp) needed the exact same
    // per-FieldValue-alternative std::visit dispatch, same reasoning as EntitySerialization.cpp
    // being extracted out of SceneSerializer.cpp for Prefab's sake. Returns true if the field
    // was edited this frame (the caller decides what "changed" means, e.g. auto-save).
    //
    // `scene`, if non-null, is only used by the EntityRef branch -- to resolve the referenced
    // entity's live name for display and validate a dropped "HIERARCHY_ENTITY" payload actually
    // refers to a live entity in it. Callers with no live Scene in scope (Material/
    // ScriptableObject asset inspectors) can omit it; an EntityRef field there just shows the
    // raw handle number instead of a resolved name.
    bool DrawFieldWidget(const FieldHandle& field, void* instance, Scene* scene = nullptr);

    // The actual per-FieldValue-alternative std::visit dispatch DrawFieldWidget wraps (Get ->
    // this -> Set-if-changed) -- pure value-in/value-out, no `instance`/FieldHandle::Get/Set
    // involved. Exposed separately so a NestedFieldValue's own inner fields (see Field.h's own
    // comment) can recurse into this directly against their own snapshotted values, since a
    // nested field's inner values live in a detached std::vector<FieldValue>, not through any
    // live `void*` instance a FieldHandle::Get/Set could operate on.
    bool DrawFieldValueWidget(const std::string& name, FieldValue& value, Scene* scene = nullptr);

}
