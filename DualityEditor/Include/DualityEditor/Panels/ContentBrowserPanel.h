#pragma once

#include <filesystem>
#include <string>

#include "DualityEditor/ThumbnailCache.h"

namespace Duality {

    struct EditorContext;

    // Unity-like Project browser: a resizable full asset tree on the left and an
    // asset grid on the right. Assets are editable; Packages are intentionally
    // browsable read-only here and are installed/enabled/removed by Package Manager.
    // Image assets show a real thumbnail, folders/other
    // files show a simple procedurally-drawn icon. Double-click a folder to
    // navigate into it, "Up" to go back; double-click a ".scene" file to open
    // it as the active scene (SceneOps::OpenScene -- stops Play first if
    // running). A single click (mouse RELEASE while still hovering the item
    // it was pressed on, not the initial press -- see OnImGuiRender's own
    // comment on `pressed`) selects a file (ctx.SelectedAssetPath, for the
    // Properties panel's asset inspector -- e.g. a ScriptableObject), so
    // starting a drag doesn't also steal Properties away from whatever field
    // is being dragged onto. Files drag out as an "ASSET_GUID" payload (see
    // PropertiesPanel's AssetRef fields / HierarchyPanel's Prefab drop
    // target) or drop onto a folder icon to move them there; dropping a file
    // in from Windows Explorer imports it (ImportFile, wired through
    // Window's own OS drop callback). Right-click empty space for "Create >
    // Folder/Scene/Material/ScriptableObject > <Type>" (the latter listing
    // whatever GameScripts currently has REGISTER_SCRIPTABLE_OBJECT'd);
    // right-click an item for Rename/Delete (Delete asks for confirmation
    // first -- there's no Recycle Bin/undo safety net here).
    class ContentBrowserPanel {
    public:
        explicit ContentBrowserPanel(const std::filesystem::path& rootDirectory);

        void OnImGuiRender(EditorContext& ctx);

        // Re-roots the browser at a different Assets folder (e.g. after
        // Open Project) and resets browsing back to that root.
        void SetRootDirectory(const std::filesystem::path& rootDirectory);

        // Copies `sourceFile` into whatever directory is currently being
        // browsed (overwriting an existing file of the same name) -- the
        // OS-file-drop-to-import path, driven by Application's Window
        // drop callback.
        void ImportFile(const std::filesystem::path& sourceFile);

    private:
        // Puts `path` into inline-rename mode (its label becomes an editable text field) --
        // used both for the "Rename" context menu item and right after "Create Folder"
        // (Explorer/Unity convention: a freshly-created folder starts in rename mode).
        void BeginRename(const std::filesystem::path& path);
        // Applies whatever's in m_RenameBuffer, renaming the file/folder (and its ".meta"
        // sidecar, if it's a file) on disk and re-registering it with AssetDatabase so any
        // AssetRef pointing at its guid keeps resolving. A no-op if the name is unchanged,
        // blank, or already taken.
        void CommitRename(EditorContext& ctx);
        // Applies regular / Ctrl / Shift selection semantics to a file cell.
        // SelectedAssetPath stays the primary item for the existing Inspector.
        void SelectAsset(EditorContext& ctx, const std::filesystem::path& path, bool additive, bool range);
        void NormalizeAssetSelection(EditorContext& ctx);
        // Copies a regular project asset beside itself using Unity's "Name (1)"
        // convention, then creates a fresh .meta GUID for the copy. Directories
        // and C++ source files deliberately stay out of this helper: recursive
        // copying would duplicate GUIDs, while copying a C++ script would create
        // a second translation unit with the same class/symbols.
        void DuplicateSelectedAsset(EditorContext& ctx);
        void CreateFolder(const std::filesystem::path& parent);
        // Opens the class-name dialog before either half of a C++ Behaviour pair is written.
        // One accepted name becomes both <Name>.h and <Name>.cpp.
        void BeginCreateScript();
        void RequestDelete(const std::filesystem::path& path, bool isDirectory);
        // Moves the asset `guid` resolves to (plus its ".meta") into `destDir` and
        // re-registers it -- the drag-a-file-onto-a-folder flow.
        void MoveAssetInto(const std::string& guid, const std::filesystem::path& destDir);
        void DrawDirectoryTree(EditorContext& ctx, const std::filesystem::path& directory, const char* label, int depth = 0);
        bool IsPackagesView(const std::filesystem::path& path) const;
        std::filesystem::path CurrentContentRoot() const;

        std::filesystem::path m_RootDirectory;
        std::filesystem::path m_PackagesDirectory;
        std::filesystem::path m_CurrentDirectory;
        ThumbnailCache m_Thumbnails;
        // Width of the left Project tree. It is intentionally panel-local (rather than a global
        // preference): each Content Browser dock can keep the amount of path/file detail its
        // author needs without affecting the scene/inspector layout.
        float m_ProjectTreeWidth = 190.0f;
        char m_SearchBuffer[128] = "";

        // Empty = nothing being renamed; otherwise the path (path.string()) of the item whose
        // label is currently an editable InputText instead of plain text.
        std::string m_RenamingPath;
        char m_RenameBuffer[260] = "";
        // Set true the same frame a rename starts so the InputText claims keyboard focus
        // exactly once (SetKeyboardFocusHere must be called before the widget it targets).
        bool m_FocusRenameField = false;
        char m_NewScriptName[128] = "NewBehaviour";
        bool m_FocusNewScriptName = false;
        // Create > Script is clicked inside ImGui's nested context-menu popup. Opening a modal
        // at that nesting level makes BeginPopupModal at the root level miss it, so defer the
        // actual OpenPopup until after that menu has closed.
        bool m_RequestCreateScriptDialog = false;
        std::string m_RangeAnchorPath;

        // Empty = no delete confirmation open; otherwise the path awaiting a Yes/Cancel
        // decision in the modal popup rendered at the bottom of OnImGuiRender.
        std::string m_PendingDeletePath;
        bool m_PendingDeleteIsDirectory = false;
    };

}
