#pragma once

namespace Duality {

    struct EditorContext;

    // Parent/child entity tree with selection, drag-and-drop reparenting and
    // sibling reordering, and "Create Entity"/"Create Child Entity" -- Unity/
    // Cocos Creator's Hierarchy panel equivalent.
    class HierarchyPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        char m_SearchBuffer[128] = "";
    };

}
