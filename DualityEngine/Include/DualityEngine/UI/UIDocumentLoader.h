#pragma once

#include <string>

#include "DualityEngine/UI/UIDocument.h"

namespace Duality {

    // Path-keyed cache over UIDocument::Load -- same reasoning as ScriptableObjectLoader's own
    // cache: a script calling Behaviour::InstantiateUIDocument(guid) repeatedly (e.g. spawning
    // a popup more than once) shouldn't re-parse the whole markup+stylesheet every time.
    class UIDocumentLoader {
    public:
        // Returns the cached UIDocument for `path`, parsing it the first time this path is
        // requested. A failed load is cached too (as an IsLoaded() == false document) so a
        // missing/malformed file doesn't get re-attempted (and re-logged) every single call.
        static const UIDocument& Load(const std::string& path);

        // Forces a re-parse of `path` from disk, replacing whatever was cached for it -- the
        // Editor's .uidoc asset inspector uses this for its own "Reload" button, since the
        // markup is hand-edited externally (no visual editor) and the cache would otherwise
        // keep serving stale content after an edit.
        static const UIDocument& Reload(const std::string& path);
    };

}
