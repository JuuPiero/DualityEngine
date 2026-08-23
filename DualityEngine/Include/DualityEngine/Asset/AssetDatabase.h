#pragma once

#include <string>

namespace Duality {

    // In-memory guid -> path index, backing AssetRef fields (see
    // Reflection/Field.h) -- resolves what an AssetRef actually points at.
    // Deliberately renderer-agnostic: a future 3D mesh/material reference
    // would resolve through this exact same index, not a parallel one.
    class AssetDatabase {
    public:
        // Recursively scans `rootDirectory` for ".meta" files and rebuilds
        // the whole index from scratch. Call once when a project loads/
        // opens; cheap enough for a project this size, not meant to run
        // every frame.
        static void Refresh(const std::string& rootDirectory);

        // Incrementally adds/updates one entry -- call right after
        // AssetMeta::EnsureMetaFile succeeds so a newly-browsed-into folder
        // doesn't need a full Refresh() to become resolvable.
        static void Register(const std::string& guid, const std::string& path);

        // Returns the asset's path, or an empty string if `guid` is
        // unknown (not yet registered, or the asset was deleted).
        static std::string ResolvePath(const std::string& guid);
    };

}
