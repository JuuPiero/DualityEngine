#include "DualityEditor/Panels/GamePanel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEditor/TypeScriptCompiler.h"
#include "DualityEngine/Project/Project.h"
#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    namespace {
        // UI rects live in this exact fixed, un-panned/un-zoomed pixel space (see
        // Components.h's UIRectComponent) -- unlike the Scene view's free-roam world camera,
        // there's no pan/zoom to fight here, so the Game panel (not ScenePanel's 2D pane) is
        // where a UI gizmo belongs: it already renders this same pixel space and already
        // computes the scale-to-fit-panel factor this needs (see OnImGuiRender below).
        constexpr float UIRectHandleHalfSize = 5.0f;

        // A point-anchor axis keeps its own Pivot as the "fixed point" while resizing; a
        // stretched axis has no pivot effect at all in ResolveUIRect (always effectively 0.5,
        // the midpoint) -- matching that here keeps corner-drag math correct in both cases.
        float EffectivePivot(const UIRectComponent& rect, int axis) {
            return (rect.AnchorMin[axis] == rect.AnchorMax[axis]) ? rect.Pivot[axis] : 0.5f;
        }

        // Adjusts AnchoredPosition/SizeDelta on one axis so the OPPOSITE edge stays fixed while
        // the dragged edge moves by `delta` UI pixels -- matches Unity's own corner-drag
        // behavior. Both ResolveUIRect branches satisfy d(rectMin)/d(AnchoredPosition) == 1, so
        // this same delta-based adjustment is correct whether the axis is point- or
        // stretch-anchored.
        void ApplyUIResizeAxis(float& anchoredPos, float& sizeDelta, float effPivot, float delta, bool isMaxEdge) {
            if (isMaxEdge) {
                anchoredPos += effPivot * delta;
                sizeDelta = std::max(4.0f, sizeDelta + delta);
            } else {
                anchoredPos += (1.0f - effPivot) * delta;
                sizeDelta = std::max(4.0f, sizeDelta - delta);
            }
        }

        // Resolves a UIRectComponent entity's parent rect the same way ResolveUIRect's own
        // internals do (root entities resolve against their owning Canvas screen) -- needed here
        // only to place the anchor marker, which ResolveUIRect itself has no reason to expose.
        void ResolveUIParentRect(Scene& scene, Entity entity, glm::vec2& outParentTopLeft, glm::vec2& outParentSize) {
            Entity parent = entity.GetComponent<HierarchyComponent>().Parent;
            if (parent && parent.HasComponent<UIRectComponent>()) {
                ResolveUIRect(scene, parent, outParentTopLeft, outParentSize);
            } else {
                outParentTopLeft = { 0.0f, 0.0f };
                Entity canvas = FindOwningCanvas(scene, entity);
                if (!canvas) {
                    outParentSize = { 0.0f, 0.0f };
                    return;
                }
                Screen canvasScreen = canvas.GetComponent<CanvasComponent>().Screen;
                outParentSize.x = (canvasScreen == Screen::Top) ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
                outParentSize.y = (canvasScreen == Screen::Top) ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);
            }
        }

        // Click-to-select + drag-to-move (body) + drag-to-resize (4 corners, selected entity
        // only) + a non-draggable anchor marker, all in this screen's own fixed UI pixel space.
        // `screenOrigin`/`scale` convert that space into the panel's own ImGui screen-space
        // coordinates -- the exact same conversion `drawScreen` already does for mouse input.
        void DrawUIRectGizmo(EditorContext& ctx, Screen screen, ImVec2 screenOrigin, float scale) {
            Scene& scene = ctx.SceneRef;
            EnsureUIElementsHaveCanvas(scene);

            struct Item { Entity E; int Sort; };
            std::vector<Item> items;
            for (auto handle : scene.Registry().view<UIRectComponent>()) {
                Entity e(handle, &scene);
                auto& rect = e.GetComponent<UIRectComponent>();
                if (!IsUIElementOnCanvas(scene, e, screen) || !rect.Enabled)
                    continue;
                int sort = rect.SortOrder;
                Entity current = e;
                while (current) {
                    if (current.HasComponent<CanvasComponent>())
                        sort += current.GetComponent<CanvasComponent>().SortOrder * 1000;
                    current = current.GetComponent<HierarchyComponent>().Parent;
                }
                items.push_back({ e, sort });
            }
            // Ascending sort order -- the topmost-drawn (highest sort) entity's button is added
            // last, so it wins overlapping hover, matching RenderScreenUI's own draw order.
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.Sort < b.Sort; });

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            auto toScreen = [&](glm::vec2 uiPixel) {
                return ImVec2(screenOrigin.x + uiPixel.x * scale, screenOrigin.y + uiPixel.y * scale);
            };

            for (auto& item : items) {
                glm::vec2 topLeft, size;
                ResolveUIRect(scene, item.E, topLeft, size);
                ImVec2 min = toScreen(topLeft);
                ImVec2 max = toScreen(topLeft + size);

                ImGui::PushID(static_cast<int>(static_cast<uint32_t>(item.E.Handle())));
                ImGui::SetCursorScreenPos(min);
                ImGui::InvisibleButton("##UIRectBody", ImVec2(max.x - min.x, max.y - min.y));
                if (ImGui::IsItemActivated()) {
                    EditorCommands::SelectOnly(ctx, item.E);
                    ctx.Session.RequestRevealSelection = true;
                }
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    auto& rect = item.E.GetComponent<UIRectComponent>();
                    rect.AnchoredPosition.x += delta.x / scale;
                    rect.AnchoredPosition.y += delta.y / scale;
                    if (delta.x != 0.0f || delta.y != 0.0f)
                        MarkSceneDirty(ctx, "Move UI Element");
                }
                ImGui::PopID();

                bool isSelected = (ctx.Selected == item.E);
                drawList->AddRect(min, max, isSelected ? IM_COL32(80, 180, 255, 255) : IM_COL32(255, 255, 255, 70),
                    0.0f, 0, isSelected ? 2.0f : 1.0f);
            }

            Entity selected = ctx.Selected;
            if (!selected || !selected.HasComponent<UIRectComponent>())
                return;
            auto& selectedRect = selected.GetComponent<UIRectComponent>();
            if (!IsUIElementOnCanvas(scene, selected, screen) || !selectedRect.Enabled)
                return;

            glm::vec2 topLeft, size;
            ResolveUIRect(scene, selected, topLeft, size);
            ImVec2 min = toScreen(topLeft);
            ImVec2 max = toScreen(topLeft + size);

            // Anchor marker -- only meaningful to draw as a single point when both axes are
            // point-anchored (the common case, and what legacy migration always produces);
            // skipped on any stretched axis rather than approximating (visual-only, no
            // regression versus today's total absence of any UI gizmo).
            if (selectedRect.AnchorMin.x == selectedRect.AnchorMax.x && selectedRect.AnchorMin.y == selectedRect.AnchorMax.y) {
                glm::vec2 parentTopLeft, parentSize;
                ResolveUIParentRect(scene, selected, parentTopLeft, parentSize);
                glm::vec2 anchorPoint = parentTopLeft + selectedRect.AnchorMin * parentSize;
                ImVec2 a = toScreen(anchorPoint);
                float k = 6.0f;
                ImVec2 pts[4] = { ImVec2(a.x, a.y - k), ImVec2(a.x + k, a.y), ImVec2(a.x, a.y + k), ImVec2(a.x - k, a.y) };
                drawList->AddConvexPolyFilled(pts, 4, IM_COL32(255, 210, 60, 255));
                drawList->AddPolyline(pts, 4, IM_COL32(120, 90, 0, 255), ImDrawFlags_Closed, 1.5f);
            }

            // 4 corner resize handles, added after every body button above so they win hover
            // where they overlap the selected rect's own body -- matches ScenePanel.cpp's
            // existing 2D collider handle draw-order convention.
            struct Corner { ImVec2 Pos; bool IsMaxX; bool IsMaxY; };
            Corner corners[4] = {
                { min, false, false }, { ImVec2(max.x, min.y), true, false },
                { ImVec2(min.x, max.y), false, true }, { max, true, true },
            };
            for (int i = 0; i < 4; i++) {
                ImGui::PushID(i);
                ImGui::SetCursorScreenPos(ImVec2(corners[i].Pos.x - UIRectHandleHalfSize, corners[i].Pos.y - UIRectHandleHalfSize));
                ImGui::InvisibleButton("##UIRectCorner", ImVec2(UIRectHandleHalfSize * 2.0f, UIRectHandleHalfSize * 2.0f));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    ApplyUIResizeAxis(selectedRect.AnchoredPosition.x, selectedRect.SizeDelta.x,
                        EffectivePivot(selectedRect, 0), delta.x / scale, corners[i].IsMaxX);
                    ApplyUIResizeAxis(selectedRect.AnchoredPosition.y, selectedRect.SizeDelta.y,
                        EffectivePivot(selectedRect, 1), delta.y / scale, corners[i].IsMaxY);
                    MarkSceneDirty(ctx, "Resize UI Element");
                }
                drawList->AddRectFilled(ImVec2(corners[i].Pos.x - UIRectHandleHalfSize, corners[i].Pos.y - UIRectHandleHalfSize),
                    ImVec2(corners[i].Pos.x + UIRectHandleHalfSize, corners[i].Pos.y + UIRectHandleHalfSize), IM_COL32(80, 180, 255, 255));
                ImGui::PopID();
            }
        }

    }

    void GamePanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.Session.ShowGame)
            return;
        if (!ImGui::Begin("Game", &ctx.Session.ShowGame)) {
            ImGui::End();
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ctx.Session.FocusedPanel = EditorPanel::Game;

        // Play / Pause / Step live in the main toolbar (Unity); the Game view keeps the
        // script tools and the Stats overlay.
        bool reloading = (ScriptEngine::GetStatus() == ReloadStatus::Running);
        ImGui::BeginDisabled(reloading);
        if (ImGui::Button(reloading ? "Reload Scripts (reloading...)" : "Reload Scripts")) {
            // Reloading while Playing would free GameScripts.dll out from under every live
            // BehaviourComponent::Instance/Destroy still pointing into it (Scene::OnRuntimeUpdate
            // or OnRuntimeStop would then call through dangling function pointers) -- stop (and
            // revert to the pre-Play snapshot, same as the "Stop" button) first, so nothing is
            // left referencing the module being unloaded and scripts get reloaded against a
            // clean, known scene state rather than whatever Play happened to leave behind. This
            // stays synchronous, on the main thread, BEFORE the async reload kicks off below --
            // see ScriptEngine::ReloadAsync's own comment on why that ordering matters.
            if (ctx.IsPlaying)
                EditorCommands::StopPlay(ctx);
            ScriptEngine::ReloadAsync(ctx.BuildDirectory);
            // TypeScript scripts rebuild too (they also hot-reload on save by themselves).
            if (auto project = Project::GetActive())
                TypeScriptCompiler::CompileAsync(ctx.RepoRoot, project->GetDirectory());
        }
        ImGui::EndDisabled();
        if (TypeScriptCompiler::HasScripts()) {
            ImGui::SameLine();
            const TsCompileStatus tsStatus = TypeScriptCompiler::GetStatus();
            ImGui::TextDisabled(tsStatus == TsCompileStatus::Running ? "TS: compiling..." : tsStatus == TsCompileStatus::Failed ? "TS: errors (see Console)" : "TS: ok");
        }

        // Stats overlay -- FPS (Editor frame rate, smoothed) and draw calls (the
        // real dual-screen render pass below, both backends are unbatched so this
        // is an exact count, not an estimate -- see IRenderer2D::GetDrawCallCount).
        ImGui::SameLine();
        ImGui::TextDisabled("| FPS: %.0f  Draw Calls: %u", ctx.Fps, ctx.GameDrawCallCount);
        ImGui::TextDisabled("Meshes: %u visible / %u culled  Lit: %u (+%u vertex-lit)  Transparent: %u  Lights: %u  Shadow map: %u  Blob: %u  Sprites: %u",
            ctx.GameRenderStats.VisibleMeshes, ctx.GameRenderStats.CulledMeshes,
            ctx.GameRenderStats.LitDrawCalls, ctx.GameRenderStats.VertexLitDrawCalls, ctx.GameRenderStats.TransparentDrawCalls,
            ctx.GameRenderStats.ActiveLights, ctx.GameRenderStats.ShadowMapCasterDrawCalls, ctx.GameRenderStats.BlobShadowDrawCalls,
            ctx.GameRenderStats.VisibleSprites);

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
            ImVec2 itemMin = ImGui::GetItemRectMin();
            // The UI gizmo below places InvisibleButtons at absolute screen positions, which
            // moves ImGui's own layout cursor around -- save/restore it so the next widget
            // (the other screen's own label/image) lands where normal top-down layout expects.
            ImVec2 afterImageCursor = ImGui::GetCursorScreenPos();

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
                ImVec2 mouse = ImGui::GetIO().MousePos;
                glm::vec2 local{ (mouse.x - itemMin.x) / scale, (mouse.y - itemMin.y) / scale };
                bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                InputManager::SetPointer(down, local, screen);
            }

            // Visual UI Transform gizmo -- Editor-only, gated on not Playing so it never
            // intercepts real touch input Play needs (see InputManager::SetPointer above).
            if (!ctx.IsPlaying)
                DrawUIRectGizmo(ctx, screen, itemMin, scale);

            ImGui::SetCursorScreenPos(afterImageCursor);
        };

        drawScreen("Top Screen (400x240)", ctx.TopFramebuffer, Screen::Top, TopScreenWidth, TopScreenHeight);
        ImGui::Spacing();
        drawScreen("Bottom Screen (320x240, touch)", ctx.BottomFramebuffer, Screen::Bottom, BottomScreenWidth, BottomScreenHeight);

        // Neither screen's image is hovered this frame -- clear rather than leave a stale
        // "still down over the last-hovered screen" state lingering (e.g. the user dragged the
        // mouse off the Game panel entirely mid-press).
        if (!anyScreenHovered)
            InputManager::SetPointer(false, { 0.0f, 0.0f }, Screen::Top);

        ImGui::End();
    }

}
