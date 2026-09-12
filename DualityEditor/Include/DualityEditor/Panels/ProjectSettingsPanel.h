#pragma once

#include "DualityEditor/ThumbnailCache.h"

namespace Duality {

    struct EditorContext;

    // Unity-inspired project-settings hub. Categories with an implemented engine subsystem
    // edit and persist real ProjectConfig values; unsupported Unity categories are explicitly
    // documented in the UI instead of exposing inert controls. Floating window
    // (ctx.ShowProjectSettings), opened via Edit > Project Settings....
    class ProjectSettingsPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        ThumbnailCache m_IconThumbnail;
    };

}
