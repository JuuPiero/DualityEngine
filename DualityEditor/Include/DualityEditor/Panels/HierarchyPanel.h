#pragma once

#include <vector>

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
        // F2 starts a focused rename modal for the current selection. Keeping the target as an
        // Entity instead of a raw NameComponent pointer makes it safe across reparent/removal.
        Entity m_Renaming;
        char m_RenameBuffer[256] = "";
        // Shift extends from this entity; Ctrl toggles an entity without moving
        // the anchor, matching the familiar Unity/Explorer selection model.
        Entity m_RangeAnchor;
        // Defer destruction until the hierarchy has finished drawing: removing a
        // node while its parent/child vectors are being traversed would invalidate
        // those traversals.
        std::vector<Entity> m_PendingRemovals;
    };

}
