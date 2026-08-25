#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Duality {

    struct EditorContext;

    // One entry per recognized project-asset file extension -- the Properties panel's generic
    // "show the right Inspector for whatever's selected in the Content Browser" dispatch,
    // mirroring how Reflection/TypeRegistry.h dispatches Entity components by type, except keyed
    // by file extension instead. Multiple extensions can share one entry's DrawInspector (e.g.
    // every recognized image extension all draw the same "Texture" importer UI) -- each is
    // registered separately, see AssetInspectors.cpp.
    struct AssetInspectorEntry {
        std::string Extension;   // lowercase, includes the dot, e.g. ".mat"
        std::string DisplayName; // shown as a header above whatever DrawInspector draws
        // `path` is the selected asset file's path; `ctx` is threaded through for the rare
        // inspector that needs live Editor state (e.g. the Texture inspector forcing a render
        // cache reload after an import-setting edit) -- most inspectors ignore it.
        std::function<void(const std::string& path, EditorContext& ctx)> DrawInspector;
    };

    class AssetInspectorRegistry {
    public:
        static void Register(const AssetInspectorEntry& entry);
        // `extension` should already be lowercased by the caller (PropertiesPanel does this
        // once per frame) -- kept a plain exact-match lookup rather than normalizing internally,
        // since every registration in AssetInspectors.cpp already writes lowercase literals.
        static const AssetInspectorEntry* FindByExtension(const std::string& extension);
    };

}
