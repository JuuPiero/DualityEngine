#pragma once

#include <functional>
#include <string>
#include <vector>

#include "DualityEngine/Scripting/ScriptModule.h"

namespace Duality {

    struct ScriptInstance; // Scene/Components.h

    // Which scripting backend supplied a registered class. C++ classes come from the
    // GameScripts module (hot-reload DLL on desktop, statically linked on 3DS); TypeScript
    // classes come from the project's compiled JS bundle running on QuickJS (see
    // Scripting/JS/JsScripting.h). Both live in the SAME registry and the same
    // BehaviourComponent/ScriptInstance slots, so Scene's lifecycle dispatch, scene
    // serialization and the Properties panel treat them identically.
    enum class ScriptLanguage {
        Cpp,
        TypeScript,
    };

    // The engine-side (host) registry Scene::OnRuntimeStart looks scripts up
    // in by class name. Populated by whatever loaded the GameScripts module
    // (today: the desktop Editor's DLL loader, after calling the module's
    // exported GetScriptFactories()) -- this class itself has no idea
    // whether that module was a DLL or statically linked -- and by
    // JsScripting when a TypeScript bundle is loaded (RegisterDynamic).
    class ScriptRegistry {
    public:
        // Drops every registered class, whatever its language.
        static void Clear();
        // Drops only one backend's classes -- a C++ DLL hot reload must not forget the
        // TypeScript classes (and vice versa).
        static void ClearLanguage(ScriptLanguage language);

        // A C++ class from a GameScripts module. Replaces any previous entry of that name.
        static void Register(const ScriptFactoryEntry& entry);

        // A class whose factory needs per-class state (a TypeScript class: the factory must
        // know WHICH JS constructor to call, which a plain `Behaviour* (*)()` can't carry).
        // Refuses (returns false, logs) when a C++ class already owns `name`.
        static bool RegisterDynamic(const std::string& name, ScriptLanguage language,
                                    std::function<Behaviour*()> create, void (*destroy)(Behaviour*),
                                    std::vector<FieldHandle> fields);

        static bool IsRegistered(const std::string& className);
        static bool TryCreate(const std::string& className, Behaviour** outInstance, void (**outDestroy)(Behaviour*));
        static int Count();

        // Every registered class name, e.g. for the Properties panel's "Add Script" submenu.
        // No particular order guaranteed (backed by an unordered_map).
        static std::vector<std::string> GetAllClassNames();

        // ScriptLanguage::Cpp for unknown names (the historical default).
        static ScriptLanguage GetLanguage(const std::string& className);

        // The DUALITY_PROPERTY-declared fields for `className` (empty if the class never used
        // that marker, or the class isn't registered at all) -- used by the Properties panel
        // to show/edit a script's own Inspector fields, and by Scene::OnRuntimeStart/
        // EntitySerialization to apply/persist each ScriptInstance's own PropertyOverrides.
        // For a TypeScript class these are its @field / auto-detected public fields.
        static const std::vector<FieldHandle>& GetFields(const std::string& className);

        // A scene loaded while `script.ClassName` wasn't registered keeps its saved overrides
        // as raw JSON (ScriptInstance::UnresolvedOverridesJson). Once the class exists, this
        // decodes them into PropertyOverrides (never replacing keys already there). Returns
        // true if anything was resolved. Called by Scene before Play, by the Properties panel
        // and by the serializer. (Defined in EntitySerialization.cpp.)
        static bool ResolvePendingOverrides(ScriptInstance& script);
    };

}
