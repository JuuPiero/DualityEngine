#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    struct EditorContext;

    // Unity's Hierarchy window: the scene's parent/child tree with multi-selection (Ctrl / Shift
    // click, arrow keys), drag & drop reparenting / reordering (multi-object), inline rename
    // (F2), active toggles (inactive objects greyed), prefab instances in blue, Alt+click to
    // expand/collapse a whole branch, auto-expand + scroll to objects selected elsewhere, and
    // the GameObject create / edit context menus shared with the main menu (EditorCommands).
    class HierarchyPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        void DrawEntityRow(EditorContext& ctx, Entity entity, int depth);
        void DrawSearchResults(EditorContext& ctx);
        void HandleRowClick(EditorContext& ctx, Entity entity);
        void HandleKeyboardNavigation(EditorContext& ctx);
        void RevealSelection(EditorContext& ctx);
        void SetExpandedRecursive(EditorContext& ctx, Entity entity, bool expanded);
        void BeginRename(EditorContext& ctx, Entity entity);
        void DrawRowContextMenu(EditorContext& ctx, Entity entity);

        char m_SearchBuffer[128] = "";
        // Foldout state keyed by stable entity id (EntityIdRegistry), so it survives Undo/Redo,
        // Play/Stop and reparenting.
        std::unordered_set<uint64_t> m_Expanded;
        bool m_SceneExpanded = true;
        // Inline rename (F2 / double-click on a selected row / right after Create Empty).
        uint64_t m_RenamingId = 0;
        char m_RenameBuffer[256] = "";
        bool m_FocusRename = false;
        // Shift extends from this entity; Ctrl toggles without moving the anchor (Unity/Explorer).
        uint64_t m_RangeAnchorId = 0;
        // Clicking an already-selected row selects only it on mouse RELEASE, so dragging a
        // multi-selection does not collapse it on mouse down.
        uint64_t m_PendingClickId = 0;
        // Rows in display order this frame (for Shift ranges and arrow-key navigation).
        std::vector<Entity> m_VisibleRows;
        std::vector<Entity> m_LastVisibleRows;
        uint64_t m_ScrollToId = 0;
        bool m_ClickedRowThisFrame = false;
        double m_HandledPingTime = -1.0;
    };

}
