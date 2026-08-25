#pragma once

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    struct EditorContext;

    // Parent/child entity tree with selection, drag-and-drop reparenting and
    // sibling reordering, creation and removal -- Unity/
    // Cocos Creator's Hierarchy panel equivalent.
    class HierarchyPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        char m_SearchBuffer[128] = "";
        // Defer destruction until the hierarchy has finished drawing: removing a
        // node while its parent/child vectors are being traversed would invalidate
        // those traversals.
        Entity m_PendingRemoval;
    };

}
