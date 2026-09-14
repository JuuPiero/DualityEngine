#pragma once

namespace Duality {

    // Registers every built-in AssetInspectorRegistry entry (Material, Prefab,
    // ScriptableObject, Scene, Texture, Audio Clip, Code/Text preview) -- called once at Editor startup, mirroring
    // Reflection::RegisterBuiltinComponents()'s own idempotent-if-called-twice convention.
    void RegisterBuiltinAssetInspectors();

}
