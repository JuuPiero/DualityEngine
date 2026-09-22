#pragma once

#include <string>

namespace Duality {

    // Editor-only visual presets. The persisted value is the corresponding stable name from
    // ToString(), rather than the ordinal, so adding/reordering presets never corrupts a user's
    // existing Preferences file.
    enum class EditorTheme {
        Blue,
        Pink,
        Dark
    };

    EditorTheme EditorThemeFromString(const std::string& name);
    const char* ToString(EditorTheme theme);

    // Applies colors to the current ImGui context immediately. It deliberately does not touch
    // fonts or UI scale, which Window owns while creating the editor context.
    void ApplyEditorTheme(EditorTheme theme);

}
