#include "DualityEditor/Panels/MenuBarPanel.h"

#include <filesystem>

#include <imgui.h>

#include "DualityEditor/BuildPipeline.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    void MenuBarPanel::OnImGuiRender(EditorContext& ctx) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !io.WantTextInput)
            SaveScene(ctx);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Project...", nullptr, false, !ctx.IsEditingPrefab))
                    ctx.RequestNewProject = true;
                if (ImGui::MenuItem("Open Project...", nullptr, false, !ctx.IsEditingPrefab))
                    ctx.RequestOpenProject = true;
                ImGui::Separator();
                const char* saveLabel = ctx.IsEditingPrefab ? " Save Prefab" : " Save Scene";
                if (ImGui::MenuItem((std::string(EditorIcons::Save) + saveLabel).c_str(), "Ctrl+S"))
                    SaveScene(ctx);
                // Reloads ctx.ScenePath from disk, discarding unsaved in-memory edits --
                // routed through SceneOps::OpenScene (clears the scene first) rather than a
                // raw Deserialize straight onto the live scene, which used to just ADD every
                // loaded entity alongside whatever was already there (a real reported bug).
                if (ImGui::MenuItem("Load Scene", nullptr, false, !ctx.IsEditingPrefab))
                    OpenScene(ctx, ctx.ScenePath);
                // Same clear-then-load, but browses to an arbitrary .scene file first --
                // Unity's own "Open Scene", unlike "Load Scene" which always targets whatever
                // ctx.ScenePath currently is.
                if (ImGui::MenuItem("Open Scene...", nullptr, false, !ctx.IsEditingPrefab))
                    ctx.RequestOpenSceneDialog = true;
                // Writes a SNAPSHOT of the current scene to a new file the user picks --
                // does not change which scene "Save Scene"/"Load Scene" above operate on.
                // See Application::SaveSceneAsFromDialog's own comment for why -- this is
                // how a project gets a second scene file for Behaviour::LoadScene to target.
                if (ImGui::MenuItem("Save Scene As...", nullptr, false, !ctx.IsEditingPrefab))
                    ctx.RequestSaveSceneAs = true;
                ImGui::Separator();
                // Async so the Editor's UI thread never blocks on a clean 3DS
                // build (can take up to a minute) -- grayed out while one is
                // already running instead of letting a second build start
                // concurrently. Progress/result show up in the Console panel
                // (Log::), same as the old synchronous version did. Also grayed out
                // during a script reload -- both eventually run `cmake --build`, see
                // BuildPipeline::BuildForPCAsync's own comment on why they share one gate.
                bool building = (BuildPipeline::GetStatus() == BuildStatus::Running) ||
                    (ScriptEngine::GetStatus() == ReloadStatus::Running);
                const std::string build3DSLabel = std::string(EditorIcons::Package) +
                    (building ? " Build for 3DS (building...)" : " Build for 3DS");
                if (ImGui::MenuItem(build3DSLabel.c_str(), nullptr, false, !building && !ctx.IsEditingPrefab)) {
                    if (ValidateSceneForRuntime(ctx, "Build")) {
                        SaveScene(ctx);
                        BuildPipeline::BuildFor3DSAsync(ctx.RepoRoot, ctx.ScenePath);
                    }
                }
                // Shares the same `building`/s_Status as "Build for 3DS" above (BuildPipeline
                // deliberately funnels both through one status flag -- see its own header
                // comment) so the two can't run concurrently against the same build tools.
                // DualityPlayerDesktop reads a project's Assets/ directly off disk at its own
                // launch time (no romfs/manifest cook step like the 3DS build), so this is just
                // an incremental rebuild of that one target -- saving the scene first still
                // matters, so the next launch sees the latest edits.
                if (ImGui::MenuItem(building ? "Build for PC (building...)" : "Build for PC", nullptr, false, !building && !ctx.IsEditingPrefab)) {
                    SaveScene(ctx);
                    BuildPipeline::BuildForPCAsync(ctx.BuildDirectory);
                }
                ImGui::Separator();
                // Built-in checkbox-style toggle (the bool* overload) rather than a manual
                // assignment -- ImGui::MenuItem itself flips *ctx.ShowBuildSettings on click.
                ImGui::MenuItem((std::string(EditorIcons::Settings) + " Build Settings...").c_str(), nullptr, &ctx.ShowBuildSettings);
                ImGui::EndMenu();
            }

            // Matches Unity's own menu placement (File > Build Settings, Edit > Project
            // Settings/Preferences) -- a familiar convention for this Unity-inspired editor.
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !ctx.IsPlaying && ctx.History.CanUndo()))
                    UndoScene(ctx);
                if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z", false, !ctx.IsPlaying && ctx.History.CanRedo()))
                    RedoScene(ctx);
                ImGui::Separator();
                ImGui::MenuItem((std::string(EditorIcons::Settings) + " Project Settings...").c_str(), nullptr, &ctx.ShowProjectSettings);
                ImGui::MenuItem((std::string(EditorIcons::Settings) + " Preferences...").c_str(), nullptr, &ctx.ShowPreferences);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem((std::string(EditorIcons::Package) + " Package Manager...").c_str(), nullptr, &ctx.ShowPackageManager);
                ImGui::EndMenu();
            }

            // Which scene "Save Scene"/"Load Scene" currently target -- easy to lose track of
            // once "Open Scene..."/Content Browser double-click/the Scene asset inspector's
            // "Open Scene" button can all change it; a plain menu bar Text widget (ImGui allows
            // arbitrary widgets between BeginMenuBar/EndMenuBar, not just BeginMenu blocks).
            std::string sceneLabel = ctx.IsEditingPrefab
                ? ("Prefab: " + std::filesystem::path(ctx.EditingPrefabPath).filename().string())
                : ("Scene: " + std::filesystem::path(ctx.ScenePath).filename().string());
            if (ctx.SceneDirty)
                sceneLabel += "*";
            ImGui::TextDisabled("  %s", sceneLabel.c_str());
            if (ctx.IsEditingPrefab) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Exit Prefab"))
                    ctx.RequestExitPrefabMode = true;
            }

            ImGui::EndMenuBar();
        }
    }

}
