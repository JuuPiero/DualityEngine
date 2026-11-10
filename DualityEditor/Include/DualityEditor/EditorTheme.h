#pragma once

#include <string>

struct ImFont;

namespace Duality {

    // Editor-only visual presets. The persisted value is the corresponding stable name from
    // ToString(), rather than the ordinal, so adding/reordering presets never corrupts a user's
    // existing Preferences file. Unity is the default (Unity's dark "Personal/Pro" skin).
    enum class EditorTheme {
        Blue,
        Pink,
        Dark,
        Unity
    };

    EditorTheme EditorThemeFromString(const std::string& name);
    const char* ToString(EditorTheme theme);

    // Applies colors to the current ImGui context immediately. It deliberately does not touch
    // fonts or UI scale, which Window owns while creating the editor context.
    void ApplyEditorTheme(EditorTheme theme);

    // Fonts loaded by Window (regular UI font and its bold face -- the same font when no bold
    // face is available). Bold is used for prefab overrides and section headers like Unity.
    void SetEditorFonts(ImFont* regular, ImFont* bold);
    ImFont* EditorRegularFont();
    ImFont* EditorBoldFont();

    // Semantic colors shared by panels (Unity's selection blue, prefab blue, muted text...).
    namespace EditorColors {
        unsigned int Selection();      // selected row / active tool
        unsigned int SelectionInactive();
        unsigned int PrefabText();     // prefab instance names in the Hierarchy
        unsigned int PrefabMissing();
        unsigned int InactiveText();   // disabled GameObjects
        unsigned int Ping();           // "ping" highlight
        unsigned int AxisX();
        unsigned int AxisY();
        unsigned int AxisZ();
    }

}
