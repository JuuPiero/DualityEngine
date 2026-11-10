#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    class Scene;

    // Unity-style property drawers for every FieldValue alternative: label on the left (a fixed
    // fraction of the Inspector width), the editor on the right; vectors as "X [] Y [] Z []";
    // colors as a swatch + hex field; asset / entity references as object fields with an icon
    // or thumbnail (click = ping the asset in the Project window / the entity in the Hierarchy,
    // double-click = select it, drag & drop to assign, a picker button for assets).
    //
    // Shared by the entity Inspector (built-in components, C++ DUALITY_PROPERTY fields and
    // TypeScript @field fields), ScriptableObject / Material inspectors and the Lighting window.

    // Per-draw presentation state, set by the caller around one DrawFieldWidget call.
    struct FieldDrawState {
        // Multi-object editing: bit i set = component i differs between the selected objects
        // (bit 0 alone for scalars / non-vector values). Mixed values are shown as "-" (Unity's
        // em dash) until edited; editing one component only changes that component everywhere.
        uint32_t MixedMask = 0;
        // Prefab override: the label is drawn bold with Unity's blue override bar.
        bool Overridden = false;
        // Optional extra items for the label's right-click menu (e.g. "Revert" / "Apply to
        // Prefab" for an overridden prefab value). Called inside the popup.
        std::function<void()> ContextMenu;
    };

    // Applies `state` to the next DrawFieldWidget / DrawFieldValueWidget call only.
    void SetNextFieldDrawState(FieldDrawState state);

    // Receives "ping this asset path" / "select this asset" / "ping this entity" requests made
    // by reference fields. Installed by the editor (Application); optional.
    struct FieldEditorHooks {
        std::function<void(const std::string& assetPath)> PingAsset;
        std::function<void(const std::string& assetPath)> SelectAsset;
        std::function<void(uint32_t entityHandle)> PingEntity;
        std::function<void(uint32_t entityHandle)> SelectEntity;
    };
    void SetFieldEditorHooks(FieldEditorHooks hooks);

    // Width reserved for property labels in the current window (Unity's EditorGUIUtility.labelWidth).
    float FieldLabelWidth();

    // Draws "label ........ [widget]" and returns the hidden ImGui id to give the widget
    // ("##label"); the next item width is set to fill the row. For custom property rows.
    std::string BeginPropertyRow(const std::string& label, int indentDepth = 0);

    bool DrawFieldWidget(const FieldHandle& field, void* instance, Scene* scene = nullptr);

    bool DrawFieldValueWidget(const std::string& name, FieldValue& value, Scene* scene = nullptr, int indentDepth = 0);

}
