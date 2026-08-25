#pragma once

#include <vector>

#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scripting/ScriptableObject.h"

namespace Duality {

    // The ABI boundary for GameScripts' ScriptableObject-derived data types -- the data-asset
    // analog of ScriptModule.h's ScriptFactoryEntry (same "module self-registers, host reads the
    // list back out" shape). Unlike ScriptFactoryEntry, each entry also carries its Fields:
    // built entirely inside GameScripts via the header-only MakeField<T>() template (Field.h),
    // so no compiled DualityEngine code needs linking to construct one -- consistent with why
    // GameScripts doesn't link DualityEngine's compiled library at all (see GameScripts/
    // CMakeLists.txt). Fields/Name/Create/Destroy crossing the module boundary in one struct
    // relies on both sides being built by the same toolchain in the same configure (true for
    // this project's desktop hot-reload DLL and 3DS static link alike) -- the same assumption
    // ScriptFactoryEntry::Create already makes by handing back a polymorphic Behaviour*.
    struct ScriptableObjectFactoryEntry {
        const char* Name;
        ScriptableObject* (*Create)();
        void (*Destroy)(ScriptableObject*);
        std::vector<FieldHandle> Fields;
    };

    using GetScriptableObjectFactoriesFn = void (*)(const ScriptableObjectFactoryEntry** outEntries, int* outCount);

}
