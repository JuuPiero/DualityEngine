#pragma once

namespace Duality {

    // Populates TypeRegistry with all built-in component types. Must be
    // called once before any Properties-panel rendering or scene
    // (de)serialization happens. Idempotent (safe to call more than once).
    void RegisterBuiltinComponents();

}
