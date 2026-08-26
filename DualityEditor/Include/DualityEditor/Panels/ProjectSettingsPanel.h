#pragma once

#include "DualityEditor/ThumbnailCache.h"

namespace Duality {

    struct EditorContext;

    // Project-level info -- Name/AssetsDirectory/ScriptsDirectory, read-only for this first pass
    // (changing Assets/Scripts directories live would desync every already-resolved asset/script
    // reference, genuinely unsafe to expose as editable yet). A starting point to grow from, not
    // a full multi-category settings system. Floating window (ctx.ShowProjectSettings), opened
    // via Edit > Project Settings... Also owns the project's 3DS build Icon picker (Project::
    // GetConfig().IconPath) -- unlike Name/Assets/Scripts above, this one IS meant to be edited
    // here, so it gets its own ThumbnailCache for a live preview.
    class ProjectSettingsPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        ThumbnailCache m_IconThumbnail;
    };

}
