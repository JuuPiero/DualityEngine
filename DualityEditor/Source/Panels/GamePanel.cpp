#include "DualityEditor/Panels/GamePanel.h"

#include <algorithm>
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

        // Stats overlay -- FPS (Editor frame rate, smoothed) and draw calls (the
        // real dual-screen render pass below, both backends are unbatched so this
        // is an exact count, not an estimate -- see IRenderer2D::GetDrawCallCount).
        ImGui::SameLine();
        ImGui::TextDisabled("| FPS: %.0f  Draw Calls: %u", ctx.Fps, ctx.GameDrawCallCount);

        ImGui::Separator();

        // Both framebuffers still render internally at the real 3DS resolution
        // (400x240/320x240 -- SceneRenderer targets those exact sizes, matching what
        // the actual on-device build produces) -- only the *displayed* image size
        // scales here, purely an Editor preview convenience. A single scale factor
        // derived from the (wider) top screen is shared by both, so their relative
        // size stays true to the real device's physical proportions instead of each
        // independently stretching to fill the panel width; the narrower bottom
        // screen is then explicitly centered in the leftover horizontal space.
        float avail = ImGui::GetContentRegionAvail().x;
        float scale = std::max(avail / static_cast<float>(TopScreenWidth), 0.05f);

        auto drawScreen = [&](const char* label, Framebuffer& framebuffer, int nativeWidth, int nativeHeight) {
            ImGui::Text("%s", label);
            float width = nativeWidth * scale;
            float height = nativeHeight * scale;
            float centerOffset = (avail - width) * 0.5f;
            if (centerOffset > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(framebuffer.GetColorAttachment())),
                         ImVec2(width, height), ImVec2(0, 1), ImVec2(1, 0));
        };

        drawScreen("Top Screen (400x240)", ctx.TopFramebuffer, TopScreenWidth, TopScreenHeight);
        ImGui::Spacing();
        drawScreen("Bottom Screen (320x240, touch)", ctx.BottomFramebuffer, BottomScreenWidth, BottomScreenHeight);

        ImGui::End();
    }

}
