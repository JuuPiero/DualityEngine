#pragma once

#include <filesystem>
#include <string>

#include "DualityEditor/ThumbnailCache.h"

namespace Duality {

    struct EditorContext;

    // Assets folder browser, grid-of-icons style (Unity/Unreal/Cocos
    // Creator-like): image assets show a real thumbnail, folders/other
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
        void CreateFolder(const std::filesystem::path& parent);
        void RequestDelete(const std::filesystem::path& path, bool isDirectory);
        // Moves the asset `guid` resolves to (plus its ".meta") into `destDir` and
        // re-registers it -- the drag-a-file-onto-a-folder flow.
        void MoveAssetInto(const std::string& guid, const std::filesystem::path& destDir);

        std::filesystem::path m_RootDirectory;
        std::filesystem::path m_CurrentDirectory;
        ThumbnailCache m_Thumbnails;
        char m_SearchBuffer[128] = "";

        // Empty = nothing being renamed; otherwise the path (path.string()) of the item whose
        // label is currently an editable InputText instead of plain text.
        std::string m_RenamingPath;
        char m_RenameBuffer[260] = "";
        // Set true the same frame a rename starts so the InputText claims keyboard focus
        // exactly once (SetKeyboardFocusHere must be called before the widget it targets).
        bool m_FocusRenameField = false;

        // Empty = no delete confirmation open; otherwise the path awaiting a Yes/Cancel
        // decision in the modal popup rendered at the bottom of OnImGuiRender.
        std::string m_PendingDeletePath;
        bool m_PendingDeleteIsDirectory = false;
    };

}
