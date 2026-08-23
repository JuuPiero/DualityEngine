#include "DualityEditor/Panels/GamePanel.h"

#include <cstdint>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    void GamePanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Game");

        if (!ctx.IsPlaying) {
            if (ImGui::Button("Play")) {
                ctx.IsPlaying = true;
                ctx.SceneRef.OnRuntimeStart();
            }
        } else {
            if (ImGui::Button("Stop")) {
                ctx.IsPlaying = false;
                ctx.SceneRef.OnRuntimeStop();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Scripts"))
            ScriptEngine::Reload(ctx.BuildDirectory);

        ImGui::Separator();
        ImGui::Text("Top Screen (400x240)");
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(ctx.TopFramebuffer.GetColorAttachment())),
                     ImVec2(static_cast<float>(TopScreenWidth), static_cast<float>(TopScreenHeight)), ImVec2(0, 1), ImVec2(1, 0));
        ImGui::Spacing();
        ImGui::Text("Bottom Screen (320x240, touch)");
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(ctx.BottomFramebuffer.GetColorAttachment())),
                     ImVec2(static_cast<float>(BottomScreenWidth), static_cast<float>(BottomScreenHeight)), ImVec2(0, 1), ImVec2(1, 0));

        ImGui::End();
    }

}
