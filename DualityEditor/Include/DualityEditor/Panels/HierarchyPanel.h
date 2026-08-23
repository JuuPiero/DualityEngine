#pragma once

namespace Duality {

    struct EditorContext;

    // Entity list with selection, plus a "Create Entity" button -- Unity's
    // Hierarchy "+"/right-click-Create-Empty equivalent.
    class HierarchyPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
