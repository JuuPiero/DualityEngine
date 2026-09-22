#include "DualityEditor/Panels/PreferencesPanel.h"

#include <imgui.h>

#include "DualityEditor/BuildPipeline.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorSettings.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"

namespace Duality {

    void PreferencesPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.ShowPreferences)
            return;
        if (!ImGui::Begin("Preferences", &ctx.ShowPreferences)) {
            ImGui::End();
            return;
        }

        EditorSettings& settings = EditorSettings::Get();

        ImGui::TextUnformatted("Appearance");
        const char* themeNames[] = { "Blue", "Pink", "Dark" };
        int themeIndex = static_cast<int>(EditorThemeFromString(settings.ColorTheme));
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("Color Theme", &themeIndex, themeNames, IM_ARRAYSIZE(themeNames))) {
            const EditorTheme selectedTheme = static_cast<EditorTheme>(themeIndex);
            settings.ColorTheme = ToString(selectedTheme);
            ApplyEditorTheme(selectedTheme);
            settings.Save();
        }
        ImGui::TextDisabled("Applies immediately and is saved for this editor installation.");
        ImGui::Separator();

        ImGui::TextUnformatted("External Editor");
        ImGui::TextDisabled("%s", settings.ExternalEditorPath.empty() ? "<not set>" : settings.ExternalEditorPath.c_str());
        // Needs the native window handle (only Application has it) -- routed through the same
        // one-shot request-flag convention RequestOpenSceneDialog already established, handled
        // once in Application::Run().
        if (ImGui::Button("Browse..."))
            ctx.RequestBrowseExternalEditor = true;

        ImGui::Spacing();
        if (ImGui::Button("Open Project in External Editor")) {
            auto project = Project::GetActive();
            if (settings.ExternalEditorPath.empty())
                Log::Warn("Preferences: no external editor set yet -- click Browse... first");
            else if (!project)
                Log::Warn("Preferences: no active project to open");
            else
                BuildPipeline::RunCommand("\"" + settings.ExternalEditorPath + "\" \"" + project->GetDirectory() + "\"");
        }

        ImGui::End();
    }

}
