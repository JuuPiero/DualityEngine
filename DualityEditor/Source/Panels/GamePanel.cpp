#include "DualityEditor/Panels/GamePanel.h"

#include <algorithm>
#include <cstdint>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    namespace {
        // Stops Play and reverts every change it made -- script-driven Transform edits,
        // physics results, runtime-spawned/destroyed entities -- back to whatever ctx.SceneRef
        // looked like the instant "Play" was pressed (ctx.PlaySnapshot, captured below).
        // Matches Unity's own Play/Stop guarantee: nothing done while Playing survives Stop.
        // Shared by the "Stop" button and "Reload Scripts"' own mid-Play stop, since both need
        // the exact same teardown-then-restore, in that order (OnRuntimeStop first -- it does
        // real cleanup, firing final OnDisable/OnDestroy and freeing the physics worlds, on the
        // scene that was actually playing -- only then is it safe to clear and reload).
        void StopPlaying(EditorContext& ctx) {
            ctx.SceneRef.OnRuntimeStop();
            ctx.SceneRef.Clear();
            SceneSerializer(ctx.SceneRef).DeserializeFromJson(nlohmann::json::parse(ctx.PlaySnapshot));
            ctx.Selected = Entity(); // old handles don't survive Clear()
            ctx.IsPlaying = false;
        }
    }

    void GamePanel::OnImGuiRender(EditorContext& ctx) {
        ImGui::Begin("Game");

        if (!ctx.IsPlaying) {
            if (ImGui::Button("Play")) {
                ctx.PlaySnapshot = SceneSerializer(ctx.SceneRef).SerializeToJson().dump();
                ctx.IsPlaying = true;
                ctx.SceneRef.OnRuntimeStart();
            }
        } else {
            if (ImGui::Button("Stop"))
                StopPlaying(ctx);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Scripts")) {
            // Reloading while Playing would free GameScripts.dll out from under every live
            // BehaviourComponent::Instance/Destroy still pointing into it (Scene::OnRuntimeUpdate
            // or OnRuntimeStop would then call through dangling function pointers) -- stop (and
            // revert to the pre-Play snapshot, same as the "Stop" button) first, so nothing is
            // left referencing the module being unloaded and scripts get reloaded against a
            // clean, known scene state rather than whatever Play happened to leave behind.
            if (ctx.IsPlaying)
                StopPlaying(ctx);
            ScriptEngine::Reload(ctx.BuildDirectory);
        }

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

        bool anyScreenHovered = false;
        auto drawScreen = [&](const char* label, Framebuffer& framebuffer, Screen screen, int nativeWidth, int nativeHeight) {
            ImGui::Text("%s", label);
            float width = nativeWidth * scale;
            float height = nativeHeight * scale;
            float centerOffset = (avail - width) * 0.5f;
            if (centerOffset > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(framebuffer.GetColorAttachment())),
                         ImVec2(width, height), ImVec2(0, 1), ImVec2(1, 0));

            // Resolves the Editor's own mouse position down to this screen's local pixel space
            // (top-left origin, Y-down, native 0-nativeWidth/0-nativeHeight range) -- the same
            // job DualityPlayerDesktop's MapWindowPointToScreen and the 3DS's own touch
            // hardware do for their platforms, needed here so Behaviour::GetPointerPosition()/
            // ScreenPointToRay3D work correctly in Play-in-Editor too, not just standalone/
            // on-device builds. Runs every frame regardless of ctx.IsPlaying (harmless --
            // nothing reads Input's pointer state outside Play) so pointer state is already
            // correct the instant Play starts, not stale from whatever it last held.
            if (ImGui::IsItemHovered()) {
                anyScreenHovered = true;
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 mouse = ImGui::GetIO().MousePos;
                glm::vec2 local{ (mouse.x - itemMin.x) / scale, (mouse.y - itemMin.y) / scale };
                bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                Input::SetPointer(down, local, screen);
            }
        };

        drawScreen("Top Screen (400x240)", ctx.TopFramebuffer, Screen::Top, TopScreenWidth, TopScreenHeight);
        ImGui::Spacing();
        drawScreen("Bottom Screen (320x240, touch)", ctx.BottomFramebuffer, Screen::Bottom, BottomScreenWidth, BottomScreenHeight);

        // Neither screen's image is hovered this frame -- clear rather than leave a stale
        // "still down over the last-hovered screen" state lingering (e.g. the user dragged the
        // mouse off the Game panel entirely mid-press).
        if (!anyScreenHovered)
            Input::SetPointer(false, { 0.0f, 0.0f }, Screen::Top);

        ImGui::End();
    }

}
