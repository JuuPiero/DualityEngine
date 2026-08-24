#include "DualityEditor/Panels/MenuBarPanel.h"

#include <imgui.h>

#include "DualityEditor/BuildPipeline.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    void MenuBarPanel::OnImGuiRender(EditorContext& ctx) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open Project..."))
                    ctx.RequestOpenProject = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene"))
                    SceneSerializer(ctx.SceneRef).Serialize(ctx.ScenePath);
                if (ImGui::MenuItem("Load Scene"))
                    SceneSerializer(ctx.SceneRef).Deserialize(ctx.ScenePath);
                // Writes a SNAPSHOT of the current scene to a new file the user picks --
                // does not change which scene "Save Scene"/"Load Scene" above operate on.
                // See Application::SaveSceneAsFromDialog's own comment for why -- this is
                // how a project gets a second scene file for Behaviour::LoadScene to target.
                if (ImGui::MenuItem("Save Scene As..."))
                    ctx.RequestSaveSceneAs = true;
                ImGui::Separator();
                // Async so the Editor's UI thread never blocks on a clean 3DS
                // build (can take up to a minute) -- grayed out while one is
                // already running instead of letting a second build start
                // concurrently. Progress/result show up in the Console panel
                // (Log::), same as the old synchronous version did.
                bool building = (BuildPipeline::GetStatus() == BuildStatus::Running);
                if (ImGui::MenuItem(building ? "Build for 3DS (building...)" : "Build for 3DS", nullptr, false, !building)) {
                    SceneSerializer(ctx.SceneRef).Serialize(ctx.ScenePath); // build packages the last-saved scene
                    BuildPipeline::BuildFor3DSAsync(ctx.RepoRoot, ctx.ScenePath);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

}
