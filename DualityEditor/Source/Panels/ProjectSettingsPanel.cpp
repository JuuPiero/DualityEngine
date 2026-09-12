#include "DualityEditor/Panels/ProjectSettingsPanel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Project/Project.h"

namespace Duality {

    namespace {
        enum class SettingsCategory {
            General,
            Player,
            Rendering,
            Physics2D,
            Physics3D,
            InputManager,
            TagsAndLayers,
            Audio,
            Time,
            Quality,
            Build,
            Editor,
        };

        constexpr const char* CategoryNames[] = {
            "General", "Player", "Rendering", "Physics 2D", "Physics 3D",
            "Input Manager", "Tags and Layers", "Audio", "Time", "Quality",
            "Build", "Editor"
        };

        void DrawUnavailable(const char* title, const char* detail) {
            ImGui::TextUnformatted(title);
            ImGui::Separator();
            ImGui::TextWrapped("%s", detail);
            ImGui::Spacing();
            ImGui::TextDisabled("This category is shown so Project Settings has a predictable Unity-style layout. Its controls will be enabled when the corresponding runtime subsystem exists.");
        }

        bool EditProjectString(const char* id, const char* hint, std::string& value, char* buffer, size_t bufferSize) {
            ImGui::SetNextItemWidth(-1.0f);
            if (!ImGui::InputTextWithHint(id, hint, buffer, bufferSize))
                return false;
            value = buffer;
            return true;
        }
    }

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

        static SettingsCategory selectedCategory = SettingsCategory::General;
        static Project* lastBoundProject = nullptr;
        static char nameBuffer[256], companyBuffer[256], versionBuffer[64], productNameBuffer[256];
        if (lastBoundProject != project.get()) {
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", project->GetConfig().Name.c_str());
            std::snprintf(companyBuffer, sizeof(companyBuffer), "%s", project->GetConfig().CompanyName.c_str());
            std::snprintf(versionBuffer, sizeof(versionBuffer), "%s", project->GetConfig().Version.c_str());
            std::snprintf(productNameBuffer, sizeof(productNameBuffer), "%s", project->GetConfig().ProductName.c_str());
            lastBoundProject = project.get();
        }

        bool changed = false;
        ImGui::BeginChild("##ProjectSettingsCategories", ImVec2(180.0f, 0.0f), true);
        ImGui::TextDisabled("PROJECT SETTINGS");
        ImGui::Separator();
        for (int i = 0; i < IM_ARRAYSIZE(CategoryNames); ++i) {
            SettingsCategory category = static_cast<SettingsCategory>(i);
            if (ImGui::Selectable(CategoryNames[i], selectedCategory == category))
                selectedCategory = category;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##ProjectSettingsContent", ImVec2(0.0f, 0.0f), true);

        switch (selectedCategory) {
            case SettingsCategory::General: {
                ImGui::TextUnformatted("General");
                ImGui::Separator();
                ImGui::TextUnformatted("Project Name");
                changed |= EditProjectString("##ProjectName", "Untitled", project->GetConfig().Name, nameBuffer, sizeof(nameBuffer));
                ImGui::TextUnformatted("Company Name");
                changed |= EditProjectString("##CompanyName", "Optional", project->GetConfig().CompanyName, companyBuffer, sizeof(companyBuffer));
                ImGui::TextUnformatted("Version");
                changed |= EditProjectString("##Version", "0.1.0", project->GetConfig().Version, versionBuffer, sizeof(versionBuffer));
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextUnformatted("Assets Directory");
                ImGui::TextDisabled("%s", project->GetAssetsDirectory().c_str());
                ImGui::TextUnformatted("Scripts Directory");
                ImGui::TextDisabled("%s", project->GetScriptsDirectory().c_str());
                ImGui::TextDisabled("Asset and script directories are intentionally read-only while a project is open.");
                break;
            }
            case SettingsCategory::Player: {
                ImGui::TextUnformatted("Player");
                ImGui::Separator();
                ImGui::TextUnformatted("Product Name");
                changed |= EditProjectString("##ProductName", project->GetConfig().Name.c_str(), project->GetConfig().ProductName, productNameBuffer, sizeof(productNameBuffer));
                ImGui::TextDisabled("Empty uses the Project Name in the 3DS home menu.");
                ImGui::Spacing();
                ImGui::TextUnformatted("Icon (3DS)");
                std::string& iconPath = project->GetConfig().IconPath;
                uint32_t iconTexture = iconPath.empty() ? 0 : m_IconThumbnail.GetThumbnail(iconPath);
                if (iconTexture != 0)
                    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(iconTexture)), ImVec2(48.0f, 48.0f));
                else
                    ImGui::TextDisabled("<using placeholder icon.png>");
                ImGui::SameLine();
                ImGui::BeginGroup();
                const std::string iconName = iconPath.empty() ? "<not set>" : std::filesystem::path(iconPath).filename().string();
                ImGui::TextDisabled("%s", iconName.c_str());
                if (ImGui::Button("Browse..."))
                    ctx.RequestBrowseIcon = true;
                ImGui::EndGroup();
                break;
            }
            case SettingsCategory::Rendering: {
                ImGui::TextUnformatted("Rendering");
                ImGui::Separator();
                ImGui::TextDisabled("2D uses Unity-style world units. PPU is applied only at the 2D render boundary.");
                float& ppu = project->GetConfig().PPU;
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::DragFloat("Pixels Per Unit", &ppu, 1.0f, 1.0f, 512.0f, "%.1f")) {
                    ppu = std::max(ppu, 1.0f);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("100 means a 1-unit sprite is drawn 100 pixels wide at Camera Zoom 1.");
                ImGui::Spacing();
                ImGui::TextUnformatted("Nintendo 3DS Resolve");
                const char* antiAliasingModes[] = { "Off", "2x1 (Recommended)", "2x2 (High VRAM)" };
                int& antiAliasingMode = project->GetConfig().N3DSAntiAliasing;
                antiAliasingMode = std::clamp(antiAliasingMode, 0, 2);
                if (ImGui::Combo("Anti-Aliasing", &antiAliasingMode, antiAliasingModes, IM_ARRAYSIZE(antiAliasingModes)))
                    changed = true;
                ImGui::TextDisabled("Applied to the next 3DS build. 2x2 may fall back if VRAM is insufficient.");
                break;
            }
            case SettingsCategory::Physics2D:
            case SettingsCategory::Physics3D: {
                const bool is2D = selectedCategory == SettingsCategory::Physics2D;
                ImGui::TextUnformatted(is2D ? "Physics 2D" : "Physics 3D");
                ImGui::Separator();
                float& gravity = project->GetConfig().Gravity;
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::DragFloat("Gravity Y", &gravity, 0.1f, -100.0f, 100.0f, "%.2f units/s^2"))
                    changed = true;
                ImGui::TextDisabled("+Y is down. The current engine intentionally shares this gravity value between Box2D and Bullet.");
                ImGui::Spacing();
                ImGui::TextDisabled("Friction, restitution, density, trigger mode and body type are per-collider/per-rigidbody Inspector settings.");
                break;
            }
            case SettingsCategory::InputManager:
                DrawUnavailable("Input Manager", "Horizontal and Vertical are available today. Desktop: A/D or Left/Right, W/S or Up/Down. 3DS: the platform host maps physical controls. Custom named axes are not implemented yet.");
                break;
            case SettingsCategory::TagsAndLayers:
                DrawUnavailable("Tags and Layers", "Tags are free-form per-entity values. Render layers currently reserve Default, TOP and BOTTOM for the dual-screen renderer; named custom layers need a runtime layer registry before they can be safely edited here.");
                break;
            case SettingsCategory::Audio:
                DrawUnavailable("Audio", "Audio import volume and AudioSource volume, looping, spatial blend and distance are implemented per asset/component. A project-wide mixer/master-volume subsystem is not implemented yet.");
                break;
            case SettingsCategory::Time:
                DrawUnavailable("Time", "Physics currently uses a fixed 1/60 second timestep with at most five catch-up steps per frame. This is intentionally fixed until script FixedUpdate and project-wide time scaling are introduced together.");
                break;
            case SettingsCategory::Quality:
                DrawUnavailable("Quality", "On Nintendo 3DS the implemented quality control is display-transfer anti-aliasing, available under Rendering. Desktop preview renders at native framebuffer resolution.");
                break;
            case SettingsCategory::Build:
                ImGui::TextUnformatted("Build");
                ImGui::Separator();
                ImGui::TextWrapped("Scene inclusion, build order and the 3DS build action live in Build Settings so they remain beside the build command.");
                if (ImGui::Button("Open Build Settings..."))
                    ctx.ShowBuildSettings = true;
                break;
            case SettingsCategory::Editor:
                ImGui::TextUnformatted("Editor");
                ImGui::Separator();
                ImGui::TextWrapped("Machine-local editor preferences, such as the external script editor, are intentionally separate from the project file.");
                if (ImGui::Button("Open Preferences..."))
                    ctx.ShowPreferences = true;
                break;
        }

        ImGui::EndChild();
        if (changed)
            project->Save();

        ImGui::End();
    }

}
