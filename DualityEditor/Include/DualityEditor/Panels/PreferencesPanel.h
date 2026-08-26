#pragma once

namespace Duality {

    struct EditorContext;

    // Editor-level (cross-project) preferences -- first feature is picking an external editor
    // (e.g. VS Code) to open the active project's folder with (see EditorSettings.h for where
    // this persists). Floating window (ctx.ShowPreferences), opened via Edit > Preferences...
    class PreferencesPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);
    };

}
