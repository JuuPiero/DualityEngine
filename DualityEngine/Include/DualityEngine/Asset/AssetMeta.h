#pragma once

#include <filesystem>
#include <string>

namespace Duality {

    // Unity/Unreal-style sidecar ".meta" file: every asset under Assets/
    // gets a companion "<name>.meta" holding a stable GUID, generated once
    // and kept forever, so an AssetRef field (see Reflection/Field.h)
    // survives the asset being renamed/moved rather than breaking like a
    // raw path would.
    class AssetMeta {
    public:
        // Creates `assetPath`'s ".meta" file with a freshly generated GUID
        // if it doesn't already exist (otherwise parses and returns the
        // existing one) -- cheap enough to call every frame as the Content
        // Browser lists a directory. Returns the guid either way.
        static std::string EnsureMetaFile(const std::filesystem::path& assetPath);
    };

}
