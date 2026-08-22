#pragma once

#include <filesystem>

#include "DualityEditor/ThumbnailCache.h"

namespace Duality {

    // Assets folder browser, grid-of-icons style (Unity/Unreal/Cocos
    // Creator-like): image assets show a real thumbnail, folders/other
    // files show a simple procedurally-drawn icon. Double-click a folder to
    // navigate into it, "Up" to go back. No import/drag-drop yet.
    class ContentBrowserPanel {
    public:
        explicit ContentBrowserPanel(const std::filesystem::path& rootDirectory);

        void OnImGuiRender();

    private:
        std::filesystem::path m_RootDirectory;
        std::filesystem::path m_CurrentDirectory;
        ThumbnailCache m_Thumbnails;
    };

}
