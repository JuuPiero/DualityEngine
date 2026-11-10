#pragma once

#include <cstdint>

namespace Duality {

    struct EditorContext;

    // Unity-style Console window: shows Duality::Log's in-memory entries (populated by engine,
    // editor and script code) with Unity's toolbar -- Clear, Collapse, Clear on Play, Error
    // Pause, search, and per-level toggles that also show how many Info / Warning / Error
    // messages there are -- plus a details pane with the full text of the selected message.
    // Purely a viewer: Log owns the entry buffer. Clear on Play / Error Pause live in
    // EditorSession because entering Play and pausing are handled by the editor commands.
    class ConsolePanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        bool m_ShowInfo = true;   // Info + Trace
        bool m_ShowWarn = true;
        bool m_ShowError = true;
        bool m_Collapse = false;
        bool m_AutoScroll = true;
        char m_SearchBuffer[128] = "";
        // Serial of the selected entry (see LogEntry::Serial), 0 = none.
        uint64_t m_SelectedSerial = 0;
        float m_DetailsHeight = 110.0f;
    };

}
