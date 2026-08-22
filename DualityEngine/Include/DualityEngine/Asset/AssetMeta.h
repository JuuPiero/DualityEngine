#pragma once

#include <filesystem>

namespace Duality {

    // Unity/Unreal-style sidecar ".meta" file: every asset under Assets/
    // gets a companion "<name>.meta" holding a stable GUID, generated once
    // and kept forever. Nothing consumes the GUID yet (no texture/prefab
    // reference fields exist), but asset references need to survive a
    // rename/move rather than break like a raw path would, so the sidecar
    // needs to already be there by the time something starts relying on it.
    class AssetMeta {
    public:
        // Creates `assetPath`'s ".meta" file with a freshly generated GUID
        // if it doesn't already exist; otherwise a no-op (one exists() check)
        // -- safe to call every frame as the Content Browser lists a
        // directory.
        static void EnsureMetaFile(const std::filesystem::path& assetPath);
    };

}
