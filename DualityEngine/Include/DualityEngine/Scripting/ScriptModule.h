#pragma once

#include "DualityEngine/Scene/Behaviour.h"

namespace Duality {

    // The ABI boundary between a GameScripts module (built as a hot-reload
    // DLL for desktop Play-in-Editor today; the same shape will support a
    // statically-linked device build in a later phase, with no script code
    // changes) and the engine/editor host that consumes it. Deliberately
    // raw function pointers, not std::function -- this struct crosses a
    // DLL boundary.
    struct ScriptFactoryEntry {
        const char* Name;
        Behaviour* (*Create)();
        void (*Destroy)(Behaviour*);
    };

    using GetScriptFactoriesFn = void (*)(const ScriptFactoryEntry** outEntries, int* outCount);

}
