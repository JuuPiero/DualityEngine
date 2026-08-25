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
                // Shares the same `building`/s_Status as "Build for 3DS" above (BuildPipeline
                // deliberately funnels both through one status flag -- see its own header
                // comment) so the two can't run concurrently against the same build tools.
                // DualityPlayerDesktop reads a project's Assets/ directly off disk at its own
                // launch time (no romfs/manifest cook step like the 3DS build), so this is just
                // an incremental rebuild of that one target -- saving the scene first still
                // matters, so the next launch sees the latest edits.
                if (ImGui::MenuItem(building ? "Build for PC (building...)" : "Build for PC", nullptr, false, !building)) {
                    SceneSerializer(ctx.SceneRef).Serialize(ctx.ScenePath);
                    BuildPipeline::BuildForPCAsync(ctx.BuildDirectory);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

}
