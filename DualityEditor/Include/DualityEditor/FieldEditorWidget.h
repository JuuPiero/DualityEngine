#pragma once

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // Draws one ImGui widget for `field`, reading/writing it on `instance` (a component on an
    // Entity, or a ScriptableObject instance -- FieldHandle::Get/Set take a plain void*, so
    // either works identically). Extracted out of PropertiesPanel.cpp once a second consumer
    // (the ScriptableObject asset inspector, also in PropertiesPanel.cpp) needed the exact same
    // per-FieldValue-alternative std::visit dispatch, same reasoning as EntitySerialization.cpp
    // being extracted out of SceneSerializer.cpp for Prefab's sake. Returns true if the field
    // was edited this frame (the caller decides what "changed" means, e.g. auto-save).
    bool DrawFieldWidget(const FieldHandle& field, void* instance);

}
