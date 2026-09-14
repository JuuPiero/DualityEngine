#pragma once

namespace Duality {

    struct EditorContext;

    // Unity-style Build Settings window: which scenes ship in a build, their order, and the
    // explicit project Start Scene. A plain floating window (ctx.ShowBuildSettings gates it),
    // not docked -- opened via File > Build Settings...
    class BuildSettingsPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
