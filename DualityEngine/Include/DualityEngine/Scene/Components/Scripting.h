#pragma once

// Script slots: BehaviourComponent holds every C++ / TypeScript script attached to an entity
// (Scene/Behaviour.h, Scripting/ScriptRegistry.h). Part of Scene/Components.h.

#include <string>
#include <unordered_map>
#include <vector>

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    class Behaviour;

    // One attached script -- which Behaviour subclass, looked up by name at Play time via
    // ScriptRegistry (not a compile-time binding, so a hot-reloaded script DLL can supply a new
    // instance without the entity needing to change). An entity can hold many of these at once
    // (see BehaviourComponent::Scripts below), matching Unity letting one GameObject carry many
    // different MonoBehaviours.
    struct ScriptInstance {
        std::string ClassName;
        // Unity's MonoBehaviour.enabled -- authored/serialized per script slot. OnUpdate and
        // collision/pointer callbacks only run when this is true AND the entity is
        // IsEffectivelyActive; flipping either edge fires OnEnable/OnDisable (see
        // WasEnabledLastFrame). Independent of ActiveComponent (entity on/off).
        bool Enabled = true;

        Behaviour* Instance = nullptr;
        void (*Destroy)(Behaviour*) = nullptr;

        // Edit-mode values for whichever DUALITY_PROPERTY fields ClassName's script declares
        // (see Reflection/PropertyMacros.h/ScriptRegistry::GetFields) -- keyed by field name.
        // Not reflected via TypeRegistry (a map isn't a plain FieldValue), so
        // EntitySerialization.cpp special-cases it alongside "Class", same as
        // HierarchyComponent's Parent. Applied onto the real Instance right before OnCreate()
        // at Play start (Scene::OnRuntimeStart); while Instance is alive, the Properties panel
        // edits it directly instead and this map is left stale until OnRuntimeStop.
        std::unordered_map<std::string, FieldValue> PropertyOverrides;

        // The saved "PropertyOverrides" JSON (as text) of keys that couldn't be decoded at load
        // time because ClassName wasn't registered yet (C++ DLL or TypeScript bundle failed to
        // build, or hasn't loaded yet). Written back verbatim on save so a broken compile never
        // erases Inspector values; decoded into PropertyOverrides once the class exists (see
        // EntitySerialization's ResolvePendingScriptOverrides).
        std::string UnresolvedOverridesJson;

        // Runtime-only (like RuntimeBody below), not reflected -- edge-detects
        // (IsEffectivelyActive && Enabled) so OnEnable/OnDisable fire once per flip.
        // Starts false so the first OnRuntimeUpdate after OnCreate sees "became enabled"
        // as a transition (Unity Awake-then-OnEnable). An entity/script that starts
        // inactive/disabled never transitions away from false until something enables it.
        bool WasEnabledLastFrame = false;
    };

    // Holds every script attached to one entity. Deliberately still ONE EnTT component type
    // (not one type per script class -- EnTT has no first-class support for runtime-registered
    // component types, and this engine's reflection is built around compile-time
    // pointer-to-member anyway) wrapping a vector of slots instead, so an entity can run several
    // different scripts (or the same one more than once, like Unity allows) simultaneously. The
    // Properties panel renders each slot as its own visually distinct card.
    struct BehaviourComponent {
        std::vector<ScriptInstance> Scripts;
    };

}
