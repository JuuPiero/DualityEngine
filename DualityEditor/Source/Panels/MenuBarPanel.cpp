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
                ImGui::Separator();
                if (ImGui::MenuItem("Build for 3DS")) {
                    SceneSerializer(ctx.SceneRef).Serialize(ctx.ScenePath); // build packages the last-saved scene
                    BuildPipeline::BuildFor3DS(ctx.RepoRoot, ctx.ScenePath);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

}
