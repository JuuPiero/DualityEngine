#pragma once

#include <cstdint>
#include <string>

namespace Duality {

    struct EditorContext;

    // Unity's Profiler (Window > Analysis > Profiler, Ctrl+7): records per-frame CPU timings
    // of the engine's phases (Core/Profiler.h markers: PlayerLoop, scripts C++ / TypeScript,
    // physics, animation / runtime systems, render 2D / 3D / UI, editor panels) plus counters
    // (draw calls, sprites, meshes, texture memory, JS heap). Shows a scrolling stacked CPU
    // graph, a Rendering / Memory chart, and the Hierarchy table of the selected frame.
    class ProfilerWindow {
    public:
        ProfilerWindow();
        void OnImGuiRender(EditorContext& ctx);
        static bool& IsOpen();
        static ProfilerWindow* Instance();

        // Selected frame (index into Profiler::Frames(), -1 = follow the newest).
        int SelectedFrame() const { return m_Selected; }
        void SelectFrame(int index) { m_Selected = index; }

    private:
        int m_Selected = -1;
        bool m_RecordOnOpen = true;
        bool m_WasOpen = false;
        int m_ChartCounter = 0;
    };

}
