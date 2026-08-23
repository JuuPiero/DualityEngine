#pragma once

namespace Duality {

    struct EditorContext;

    // File menu (Save Scene / Load Scene / Build for 3DS) drawn into the
    // dockspace host window's menu bar -- moved out of the Game panel's
    // toolbar since a menu bar is the standard place for these, matching
    // Unity/MyGameEngine. Must be called between that host window's
    // ImGui::Begin(..., ImGuiWindowFlags_MenuBar) and ImGui::End().
    class MenuBarPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
