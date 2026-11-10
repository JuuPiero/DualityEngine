#pragma once

#include <string>
#include <vector>

#include "DualityEditor/SceneHistory.h"
#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    struct EditorContext;
    struct ComponentTypeInfo;

    // The editor's shared commands -- one implementation behind every menu item, context menu,
    // toolbar button and keyboard shortcut (Unity's Edit / GameObject menus), so the Hierarchy,
    // the Scene view and the main menu can never drift apart.
    namespace EditorCommands {

        // ---- Selection -------------------------------------------------------------------
        bool IsSelected(const EditorContext& ctx, Entity entity);
        void SelectOnly(EditorContext& ctx, Entity entity);
        void SetSelection(EditorContext& ctx, const std::vector<Entity>& entities, Entity primary = {});
        // Ctrl-click: adds or removes `entity`; the primary becomes `entity` when added.
        void ToggleSelected(EditorContext& ctx, Entity entity);
        void ClearSelection(EditorContext& ctx);
        void SelectAll(EditorContext& ctx);
        // Drops destroyed entities and keeps Selected / SelectedEntities consistent (a panel that
        // only assigned ctx.Selected gets a one-entity selection). Run once per frame.
        void NormalizeSelection(EditorContext& ctx);
        // The selection without entities whose ancestor is also selected (what Duplicate /
        // Delete / Copy / gizmo drags operate on), in hierarchy order.
        std::vector<Entity> TopLevelSelection(EditorContext& ctx);
        HistorySelection CaptureSelection(EditorContext& ctx);
        void ApplySelection(EditorContext& ctx, const HistorySelection& selection);

        // ---- Entity commands (each records one Undo step) --------------------------------
        void DuplicateSelection(EditorContext& ctx);
        void DeleteSelection(EditorContext& ctx);
        void CopySelection(EditorContext& ctx);
        void CutSelection(EditorContext& ctx);
        // Paste: copies go under the parent of the originals (if it still exists). Paste As
        // Child: under the primary selection.
        void Paste(EditorContext& ctx, bool asChild = false);
        bool HasClipboard(const EditorContext& ctx);
        Entity CreateEmpty(EditorContext& ctx, Entity parent = {});
        Entity CreateEmptyParent(EditorContext& ctx);
        // Toggles ActiveComponent on the selection (Unity: Alt+Shift+A).
        // Writes `entity` (+ descendants) to Assets/Prefabs/<Name>.prefab and links it as an instance.
        void CreatePrefab(EditorContext& ctx, Entity entity);
        void ToggleActiveSelection(EditorContext& ctx);
        void SetActive(EditorContext& ctx, Entity entity, bool active);
        // The GameObject-style creation menu (Create Empty, 3D Object, 2D Object, Light, Audio,
        // Effects, UI, Camera...) used by the main menu and every Hierarchy context menu.
        void DrawCreateObjectMenu(EditorContext& ctx, Entity parent = {});
        // Unity's Edit/GameObject entity items (Cut/Copy/Paste/Duplicate/Rename/Delete).
        void DrawEntityEditMenuItems(EditorContext& ctx);

        // ---- Components ----------------------------------------------------------------------
        // Unity-style Add Component categories ("Rendering", "Physics 2D", "UI"...) for the
        // built-in components, in menu order.
        const std::vector<const char*>& ComponentCategories();
        const char* ComponentCategoryOf(const std::string& componentDisplayName);
        // Adds `type` to every selected entity that lacks it (one Undo step).
        void AddComponentToSelection(EditorContext& ctx, ComponentTypeInfo& type);

        // ---- Play mode ---------------------------------------------------------------------
        void TogglePlay(EditorContext& ctx);
        void StartPlay(EditorContext& ctx);
        void StopPlay(EditorContext& ctx);
        void TogglePause(EditorContext& ctx);
        // Advances one frame while paused (starts Play paused when not playing, like Unity).
        void Step(EditorContext& ctx);

        // ---- Keyboard -----------------------------------------------------------------------
        // Unity's global shortcuts (Undo/Redo, Save, Play/Pause/Step, tool hotkeys, Create Empty,
        // and -- while the Hierarchy or Scene view is focused -- Duplicate/Delete/Copy/Cut/
        // Paste/Select All/Rename/Frame). Called once per frame before the panels.
        void HandleGlobalShortcuts(EditorContext& ctx);
    }

}
