#include "DualityEditor/Panels/BuildSettingsPanel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "DualityEditor/BuildPipeline.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    void BuildSettingsPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.ShowBuildSettings)
            return;
        if (!ImGui::Begin("Build Settings", &ctx.ShowBuildSettings)) {
            ImGui::End();
            return;
        }

        auto project = Project::GetActive();
        if (!project) {
            ImGui::TextDisabled("<no active project>");
            ImGui::End();
            return;
        }

        std::vector<std::string>& scenesInBuild = project->GetConfig().ScenesInBuild;
        bool changed = false;

        ImGui::TextUnformatted("Nintendo 3DS");
        const char* antiAliasingModes[] = { "Off", "2x1 (Recommended)", "2x2 (High VRAM)" };
        int& antiAliasingMode = project->GetConfig().N3DSAntiAliasing;
        antiAliasingMode = std::clamp(antiAliasingMode, 0, 2);
        if (ImGui::Combo("Anti-Aliasing", &antiAliasingMode, antiAliasingModes, IM_ARRAYSIZE(antiAliasingModes)))
            changed = true;
        ImGui::TextDisabled("Uses the 3DS display-transfer resolve. 2x2 can fall back to Off if VRAM is insufficient.");
        ImGui::Separator();

        ImGui::TextUnformatted("Scenes In Build");
        ImGui::TextDisabled("Drag to reorder -- the top scene is the Start Scene.");
        ImGui::Separator();

        // Standard ImGui "drag to reorder" idiom (see Dear ImGui's own demo) -- every row here
        // always participates (none are hidden/filtered mid-list), so this is a simple, uniform
        // adjacent-index swap with no interleaving to reason about.
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(scenesInBuild.size()); i++) {
            ImGui::PushID(i);
            std::string label = (i == 0 ? "[Main] " : "") + scenesInBuild[i];
            const float removeButtonWidth = 24.0f;
            const float rowWidth = ImGui::GetContentRegionAvail().x - removeButtonWidth - ImGui::GetStyle().ItemInnerSpacing.x;
            // Reserve a real, non-overlapping column for the remove button. The old
            // SameLine(GetContentRegionAvail...) call interpreted "available width" as an
            // absolute cursor coordinate, allowing the row's drag/select item to cover the x
            // button in some dock/window sizes.
            ImGui::Selectable(label.c_str(), false, 0, ImVec2(std::max(rowWidth, 1.0f), 0.0f));

            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                int next = i + (ImGui::GetMouseDragDelta().y < 0.0f ? -1 : 1);
                if (next >= 0 && next < static_cast<int>(scenesInBuild.size())) {
                    std::swap(scenesInBuild[i], scenesInBuild[next]);
                    ImGui::ResetMouseDragDelta();
                    changed = true;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("x##RemoveScene", ImVec2(removeButtonWidth, 0.0f)))
                removeIndex = i;
            ImGui::PopID();
        }

        if (removeIndex >= 0) {
            scenesInBuild.erase(scenesInBuild.begin() + removeIndex);
            changed = true;
        }
        if (scenesInBuild.empty())
            ImGui::TextDisabled("<no scenes added yet -- click one below>");

        ImGui::Separator();
        ImGui::TextUnformatted("Other Scenes In Project");

        // Recursive scan, same fs::recursive_directory_iterator + relative-path idiom
        // BuildPipeline::CookAssets already uses -- scanned fresh every time this window
        // renders (cheap at this project's scale), so a scene created via Content Browser's
        // "Create > Scene" shows up here with no manual refresh step.
        namespace fs = std::filesystem;
        fs::path assetsDir(project->GetAssetsDirectory());
        if (fs::exists(assetsDir)) {
            std::error_code ec;
            for (auto& entry : fs::recursive_directory_iterator(assetsDir, ec)) {
                if (entry.is_directory() || entry.path().extension() != ".scene")
                    continue;
                std::string relPath = fs::relative(entry.path(), assetsDir).generic_string();
                if (std::find(scenesInBuild.begin(), scenesInBuild.end(), relPath) != scenesInBuild.end())
                    continue; // already listed above
                if (ImGui::Selectable(relPath.c_str())) {
                    scenesInBuild.push_back(relPath);
                    changed = true;
                }
            }
        }

        if (changed)
            project->Save();

        // Build button + progress -- lets a scene list get configured and built without leaving
        // this window, instead of closing it and going back to File > Build for 3DS. Shares
        // BuildPipeline's own single-build-at-a-time gate (also checks ScriptEngine, same as
        // MenuBarPanel's own "Build for 3DS" item -- both eventually run `cmake --build`, which
        // doesn't tolerate concurrent invocations against the same tree).
        ImGui::Separator();
        bool building = (BuildPipeline::GetStatus() == BuildStatus::Running) || (ScriptEngine::GetStatus() == ReloadStatus::Running);
        ImGui::BeginDisabled(building);
        if (ImGui::Button("Build for 3DS", ImVec2(-1.0f, 0.0f))) {
            SceneSerializer(ctx.SceneRef).Serialize(ctx.ScenePath); // build packages the last-saved scene, same as MenuBarPanel's own click handler
            BuildPipeline::BuildFor3DSAsync(ctx.RepoRoot, ctx.ScenePath);
        }
        ImGui::EndDisabled();

        // BuildPipeline only ever reports Idle/Running/Succeeded/Failed -- no real incremental
        // percentage exists to show (a clean 3DS build has no single "step count" this codebase
        // tracks) -- a fraction cycling with wall-clock time gives a moving "still working" cue
        // instead of a bar frozen at some arbitrary value, same purpose as MenuBarPanel's own
        // "(building...)" label suffix.
        BuildStatus status = BuildPipeline::GetStatus();
        if (status == BuildStatus::Running) {
            float fraction = std::fmod(static_cast<float>(ImGui::GetTime()), 1.0f);
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), "Building...");
        } else if (status == BuildStatus::Succeeded) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Build succeeded");
        } else if (status == BuildStatus::Failed) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Build failed -- see Console");
        }

        ImGui::End();
    }

}
