#include "DualityEditor/Panels/ProjectSettingsPanel.h"

#include <cstdint>
#include <cstdio>
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

        // Also editable here -- the name shown on the 3DS home menu / Citra's game list under
        // the icon above (see ProjectConfig::ProductName's own comment). Empty (the default)
        // falls back to this project's own Name at build time, shown as a placeholder so it's
        // clear what "leave this blank" actually does rather than looking broken/unset.
        ImGui::Separator();
        ImGui::Text("Product Name (3DS)");
        static char productNameBuffer[256];
        static Project* lastBoundProject = nullptr;
        if (lastBoundProject != project.get()) {
            std::snprintf(productNameBuffer, sizeof(productNameBuffer), "%s", project->GetConfig().ProductName.c_str());
            lastBoundProject = project.get();
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##ProductName", project->GetConfig().Name.c_str(), productNameBuffer, sizeof(productNameBuffer))) {
            project->GetConfig().ProductName = productNameBuffer;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            project->Save();

        ImGui::Separator();
        ImGui::Text("Physics");
        ImGui::TextDisabled("Scene stays in pixels. Physics uses units = pixels / PPU. 1 unit is not a meter.");
        float& ppu = project->GetConfig().PPU;
        float& gravity = project->GetConfig().Gravity;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::DragFloat("PPU", &ppu, 1.0f, 1.0f, 256.0f, "%.1f"))
            ppu = ppu < 0.001f ? 0.001f : ppu;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pixels Per Unit. Box2D is happiest around 0.1-10 units; 32 or 100 is a common 2D scale.");
        if (ImGui::IsItemDeactivatedAfterEdit())
            project->Save();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::DragFloat("Gravity", &gravity, 1.0f, -2000.0f, 2000.0f, "%.1f px/s^2"))
            ;
        if (ImGui::IsItemDeactivatedAfterEdit())
            project->Save();
        float scale = ppu < 0.001f ? 0.001f : ppu;
        ImGui::TextDisabled("Physics gravity: %.2f units/s^2", gravity / scale);

        ImGui::End();
    }

}
