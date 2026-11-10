#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "DualityEditor/EntityIdRegistry.h"
#include "DualityEditor/SceneHistory.h"

namespace Duality {

    // Scene view pane toolbar toggles (PiaEngine: Grid / Gizmos).
    struct SceneViewPaneOptions {
        bool ShowGrid = true;
        bool ShowGizmos = true;
    };

    struct EditorContext;

    // Which editor window last had keyboard focus -- scene-object shortcuts (Ctrl+D, Delete,
    // Ctrl+C/V, F2, F) act on the entity selection only while the Hierarchy or Scene view is the
    // active window, like Unity; Ctrl+D in the Project window duplicates assets instead.
    enum class EditorPanel { None, Hierarchy, Scene, Game, Inspector, Project, Console, Other };

    // Unity's Tool Handle Position (Pivot / Center) and Tool Handle Rotation (Local / Global).
    enum class HandlePosition { Pivot, Center };
    enum class HandleSpace { Local, Global };

    // Long-lived editor UI state that is not part of the Scene and that several panels share
    // (owned by Application, reached through EditorContext::Session). Grouping it here keeps
    // EditorContext's positional initializer from growing with every new feature.
    struct EditorSession {
        // ---- Stable entity identity + Undo -------------------------------------------------
        EntityIdRegistry Ids;
        // Label of the change currently being recorded ("Move", "Add Component"...). The first
        // MarkSceneDirty of a change group wins; cleared when the group is committed.
        std::string PendingUndoLabel;
        // Selection when the current change group started (restored by Undo of that change).
        HistorySelection SelectionBeforeChange;
        HistorySelection FrameStartSelection;
        bool ChangeGroupOpen = false;

        // ---- Play mode ---------------------------------------------------------------------
        bool IsPaused = false;
        bool StepRequested = false;
        // Console options that affect entering Play (Unity: Clear on Play, Error Pause).
        bool ClearConsoleOnPlay = true;
        bool ErrorPause = false;
        uint64_t ErrorCountAtLastCheck = 0;
        // Entity ids + selection at the moment Play started (the scene JSON itself lives in
        // EditorContext::PlaySnapshot), so Stop restores selection and Hierarchy state too.
        std::vector<uint64_t> PlaySnapshotIds;
        HistorySelection PlaySelection;

        // ---- Clipboard (entities) ------------------------------------------------------------
        // PrefabSerializer::CopyToJson output, plus the stable ids of every entity the copy
        // referenced by raw handle (external EntityRef targets, original parents) so Paste
        // still finds them after Undo/Redo rebuilt every handle.
        std::string ClipboardJson;
        std::unordered_map<uint32_t, uint64_t> ClipboardHandleIds;

        // ---- Requests consumed by a specific panel ------------------------------------------
        bool RequestRename = false;          // Hierarchy: start inline rename of the primary selection (F2)
        bool RequestFrameSelected = false;   // Scene view: frame the selection (F)
        bool RequestRevealSelection = false; // Hierarchy: expand ancestors + scroll to the selection
        bool RequestResetLayout = false;     // Window > Layouts > Reset
        bool RequestQuit = false;            // File > Exit (Application asks to save first)
        std::string RequestOpenProjectPath;  // File > Open Recent (Application asks to save first)
        // "Ping": highlight an asset in the Project window / an entity in the Hierarchy for a
        // moment (Unity's click-on-reference behaviour).
        std::string PingAssetPath;
        uint64_t PingEntityId = 0;
        double PingStartTime = -100.0;

        // ---- Window visibility (Window menu) ----------------------------------------------
        bool ShowScene = true;
        bool ShowGame = true;
        bool ShowHierarchy = true;
        bool ShowInspector = true;
        bool ShowProject = true;
        bool ShowConsole = true;

        EditorPanel FocusedPanel = EditorPanel::None;
        // A tool window with its own undo stack (Sprite Editor: unapplied import edits) installs
        // this while it is the focused window (FocusedPanel == Other); Ctrl+Z / Ctrl+Y /
        // Ctrl+Shift+Z then go to it instead of the scene history. Argument: true = redo.
        std::function<void(bool)> FocusedWindowUndo;
        // The Animation window's edit-mode preview writes sampled values into the live scene
        // without dirtying it (Unity's Animation preview). Everything that serializes or
        // snapshots the scene (Save, Undo/Redo, history capture, entering Play) brackets
        // itself with ScenePreviewHook(true) = put the authored values back, and
        // ScenePreviewHook(false) = re-apply the preview. Empty when nothing previews.
        std::function<void(bool)> ScenePreviewHook;

        // ---- Scene view tools ----------------------------------------------------------------
        HandlePosition ToolHandlePosition = HandlePosition::Pivot;
        HandleSpace ToolHandleSpace = HandleSpace::Global;
        // Increment snapping applied while Ctrl is held (Unity's default Increment Snap values).
        float SnapMove = 0.25f;
        float SnapRotate = 15.0f;
        float SnapScale = 0.1f;
        // Per Scene pane (0 = Top, 1 = Bottom) toolbar toggles: Grid, Gizmos.
        SceneViewPaneOptions ScenePaneOptions[2];
        // Set when a pane camera moved; ScenePanel saves the per-scene view state when the
        // navigation ends (SceneViewState.h).
        bool SceneViewStateDirty = false;

        // ---- Unsaved changes ----------------------------------------------------------------
        // Runs `action` after asking Save / Don't Save / Cancel when the scene is dirty (runs it
        // immediately otherwise). Installed by Application; every scene/project switch goes
        // through it so no edit is ever discarded silently.
        // The action receives the EditorContext of the frame it finally runs in (the prompt may
        // be answered several frames later; EditorContext itself is rebuilt every frame).
        std::function<void(std::function<void(EditorContext&)>)> RunAfterSavePrompt;
    };

}
