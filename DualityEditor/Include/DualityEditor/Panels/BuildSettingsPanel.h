#pragma once

namespace Duality {

    struct EditorContext;

    // Unity-style Build Settings window: which scenes ship in a build, in what order --
    // index 0 is the Start Scene (see ProjectConfig::ScenesInBuild's own comment). A plain
    // floating window (ctx.ShowBuildSettings gates it), not docked -- opened via File > Build
    // Settings...
    class BuildSettingsPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
