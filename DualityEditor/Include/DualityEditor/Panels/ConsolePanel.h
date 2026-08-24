#pragma once

namespace Duality {

    // Unity-style Console window: shows Duality::Log's in-memory entries
    // (already populated by engine/editor code -- Scene, ScriptEngine,
    // BuildPipeline, Project, SceneSerializer) with per-level filtering,
    // color coding and a Clear button. Purely a viewer -- Log itself owns
    // the actual entry buffer, so nothing here needs to persist state beyond
    // the filter toggles.
    class ConsolePanel {
    public:
        void OnImGuiRender();

    private:
        bool m_ShowTrace = true;
        bool m_ShowInfo = true;
        bool m_ShowWarn = true;
        bool m_ShowError = true;
        bool m_AutoScroll = true;
        char m_SearchBuffer[128] = "";
    };

}
