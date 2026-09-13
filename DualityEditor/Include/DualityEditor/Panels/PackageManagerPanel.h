#pragma once

namespace Duality {

    struct EditorContext;

    // Project-local extension manager. A package owns its code and manifest under
    // <Project>/Packages/<id>/; enabling it only changes ProjectConfig until scripts are rebuilt.
    class PackageManagerPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
