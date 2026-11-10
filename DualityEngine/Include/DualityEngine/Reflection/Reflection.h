#pragma once

namespace Duality {

    // Populates TypeRegistry with all built-in component types (one registration file per
    // domain: DualityEngine/Source/Scene/Components/<Domain>Components.cpp). Must be called once
    // before any Properties-panel rendering or scene (de)serialization happens. Idempotent.
    void RegisterBuiltinComponents();

}
