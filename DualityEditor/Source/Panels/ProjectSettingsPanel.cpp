#include "DualityEditor/Panels/ProjectSettingsPanel.h"

#include <cstdint>
#include <filesystem>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Project/Project.h"

namespace Duality {

    void ProjectSettingsPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.ShowProjectSettings)
            return;
        if (!ImGui::Begin("Project Settings", &ctx.ShowProjectSettings)) {
            ImGui::End();
            return;
        }

        auto project = Project::GetActive();
        if (!project) {
            ImGui::TextDisabled("<no active project>");
            ImGui::End();
            return;
        }

        // Read-only for this first pass -- see this panel's own header comment for why
        // AssetsDirectory/ScriptsDirectory in particular aren't safe to edit live yet.
        ImGui::Text("Name");
        ImGui::TextDisabled("%s", project->GetConfig().Name.c_str());
        ImGui::Separator();
        ImGui::Text("Assets Directory");
        ImGui::TextDisabled("%s", project->GetAssetsDirectory().c_str());
        ImGui::Separator();
        ImGui::Text("Scripts Directory");
        ImGui::TextDisabled("%s", project->GetScriptsDirectory().c_str());

        // Unlike the three fields above, this one really is meant to be edited here -- the
        // 3DS build's ".cia" icon (see ProjectConfig::IconPath's own comment for how it threads
        // through BuildPipeline -> build-3ds.bat -> DualityPlayer/CMakeLists.txt).
        ImGui::Separator();
        ImGui::Text("Icon (3DS)");
        std::string& iconPath = project->GetConfig().IconPath;
        uint32_t iconTexture = iconPath.empty() ? 0 : m_IconThumbnail.GetThumbnail(iconPath);
        if (iconTexture != 0)
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(iconTexture)), ImVec2(48.0f, 48.0f));
        else
            ImGui::TextDisabled("<using placeholder icon.png>");
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", iconPath.empty() ? "<not set>" : std::filesystem::path(iconPath).filename().string().c_str());
        // Needs the native window handle (only Application has it) -- routed through the same
        // one-shot request-flag convention RequestOpenSceneDialog/RequestBrowseExternalEditor
        // already established, handled once in Application::Run().
        if (ImGui::Button("Browse..."))
            ctx.RequestBrowseIcon = true;
        ImGui::EndGroup();

        ImGui::End();
    }

}
