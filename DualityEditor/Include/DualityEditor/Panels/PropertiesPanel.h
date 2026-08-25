#pragma once

#include <string>

#include "DualityEngine/ECS/Entity.h"

namespace Duality {

    struct EditorContext;

    // Reflection-driven field editor for every component on the selected
    // entity (unchanged from before this panel was extracted), plus:
    // "..." per non-mandatory component header opens a small popup to
    // remove it (Transform/Name/Tag have no such option -- they're
    // mandatory), and a "+ Add Component" popup at the bottom lists every
    // registered component type not already present.
    class PropertiesPanel {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        // Unity-Inspector-style "Lock" toggle -- while true, this panel keeps showing whatever
        // was selected at the moment it was locked (m_LockedEntity/m_LockedAssetPath) instead of
        // following ctx.Selected/ctx.SelectedAssetPath live. Needed because ContentBrowserPanel's
        // click-to-select fires on mouse-DOWN, the same frame a drag-and-drop onto an AssetRef
        // field begins -- without a way to pin the target entity's fields in view, starting a
        // drag from the Content Browser immediately swaps Properties away from the very field
        // being dragged onto.
        bool m_Locked = false;
        Entity m_LockedEntity;
        std::string m_LockedAssetPath;
    };

}
