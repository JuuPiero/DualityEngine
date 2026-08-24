#pragma once

#include <filesystem>

#include "DualityEditor/ThumbnailCache.h"

namespace Duality {

    // Assets folder browser, grid-of-icons style (Unity/Unreal/Cocos
    // Creator-like): image assets show a real thumbnail, folders/other
    // files show a simple procedurally-drawn icon. Double-click a folder to
    // navigate into it, "Up" to go back. Files drag out as an "ASSET_GUID"
    // payload (see PropertiesPanel's AssetRef fields / HierarchyPanel's Prefab
    // drop target); dropping a file in from Windows Explorer imports it
    // (ImportFile, wired through Window's own OS drop callback).
    class ContentBrowserPanel {
    public:
        explicit ContentBrowserPanel(const std::filesystem::path& rootDirectory);

        void OnImGuiRender();

        // Re-roots the browser at a different Assets folder (e.g. after
        // Open Project) and resets browsing back to that root.
        void SetRootDirectory(const std::filesystem::path& rootDirectory);

        // Copies `sourceFile` into whatever directory is currently being
        // browsed (overwriting an existing file of the same name) -- the
        // OS-file-drop-to-import path, driven by Application's Window
        // drop callback.
        void ImportFile(const std::filesystem::path& sourceFile);

    private:
        std::filesystem::path m_RootDirectory;
        std::filesystem::path m_CurrentDirectory;
        ThumbnailCache m_Thumbnails;
        char m_SearchBuffer[128] = "";
    };

}
