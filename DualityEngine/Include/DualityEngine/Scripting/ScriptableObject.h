#pragma once

namespace Duality {

    // Unity's ScriptableObject-equivalent: a reusable data container that lives as its own
    // project asset (a ".asset" file, see Asset/ScriptableObjectLoader.h) instead of being
    // attached to an Entity -- e.g. a shared "GameSettings" data asset several scripts all read
    // from, edited once via the Properties panel instead of duplicated per-entity as component
    // fields. Subclassed in GameScripts exactly like Behaviour, but self-registers via
    // REGISTER_SCRIPTABLE_OBJECT (ScriptableObjectRegistration.h) instead of REGISTER_BEHAVIOUR,
    // and declares its reflected fields via a `static std::vector<FieldHandle> Fields()` method
    // (same MakeField() calls Reflection.cpp uses for built-in components) since a
    // ScriptableObject has no Entity to attach per-field metadata to via TypeRegistry.
    class ScriptableObject {
    public:
        virtual ~ScriptableObject() = default;
    };

}
