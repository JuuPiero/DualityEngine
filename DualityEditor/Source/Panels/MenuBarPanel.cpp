#include "DualityEditor/Panels/MenuBarPanel.h"

#include <filesystem>

#include <imgui.h>

#include "DualityEditor/BuildPipeline.h"
#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorIcons.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEditor/EditorSettings.h"
#include "DualityEditor/Panels/LightingPanel.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEditor/Windows/AnimationWindow.h"
#include "DualityEditor/Windows/ProfilerWindow.h"
#include "DualityEditor/Windows/SpriteEditorWindow.h"
#include "DualityEditor/Windows/TilePaletteWindow.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/SceneSerializer.h"

namespace Duality {

    namespace {
        std::string WithIcon(const char* icon, const char* label) {
            return std::string(icon) + "  " + label;
        }
    }

    // Unity's main menu: File / Edit / Assets / GameObject / Component / Window / Help. Every
    // item calls the same command used by its keyboard shortcut and context-menu twin
    // (EditorCommands / SceneOps), so the three entry points can never behave differently.
    void MenuBarPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("File")) {
            const bool sceneFile = !ctx.IsEditingPrefab;
            if (ImGui::MenuItem("New Scene", "Ctrl+N", false, sceneFile))
                NewSceneWithPrompt(ctx);
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, sceneFile))
                ctx.RequestOpenSceneDialog = true;
            // Reloads ctx.ScenePath from disk, discarding unsaved in-memory edits (asks first).
            if (ImGui::MenuItem("Revert Scene", nullptr, false, sceneFile && !ctx.ScenePath.empty()))
                OpenSceneWithPrompt(ctx, ctx.ScenePath);
            ImGui::Separator();
            if (ImGui::MenuItem(WithIcon(EditorIcons::Save, ctx.IsEditingPrefab ? "Save Prefab" : "Save").c_str(), "Ctrl+S"))
                SaveScene(ctx);
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, sceneFile))
                ctx.RequestSaveSceneAs = true;
            ImGui::Separator();
            if (ImGui::MenuItem("New Project...", nullptr, false, sceneFile))
                ctx.RequestNewProject = true;
            if (ImGui::MenuItem(WithIcon(EditorIcons::FolderOpen, "Open Project...").c_str(), nullptr, false, sceneFile))
                ctx.RequestOpenProject = true;
            const auto& recent = EditorSettings::Get().RecentProjects;
            if (ImGui::BeginMenu("Open Recent Project", sceneFile && !recent.empty())) {
                for (const std::string& path : recent) {
                    const bool exists = std::filesystem::exists(path);
                    const std::string label = std::filesystem::path(path).stem().string() + "##" + path;
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, exists))
                        ctx.Session.RequestOpenProjectPath = path;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", path.c_str());
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem(WithIcon(EditorIcons::Settings, "Build Settings...").c_str(), "Ctrl+Shift+B", &ctx.ShowBuildSettings);
            // Async so the Editor's UI thread never blocks on a clean 3DS build -- grayed out
            // while a build or a script reload is already running (they share one gate, see
            // BuildPipeline::BuildForPCAsync). Progress/result show up in the Console.
            const bool building = (BuildPipeline::GetStatus() == BuildStatus::Running) ||
                (ScriptEngine::GetStatus() == ReloadStatus::Running);
            if (ImGui::MenuItem(WithIcon(EditorIcons::Package, building ? "Build for 3DS (building...)" : "Build for 3DS").c_str(), nullptr, false, !building && sceneFile && !ctx.IsPlaying)) {
                if (ValidateSceneForRuntime(ctx, "Build") && SaveScene(ctx))
                    BuildPipeline::BuildFor3DSAsync(ctx.RepoRoot, ctx.ScenePath);
            }
            // DualityPlayerDesktop reads a project's Assets/ directly off disk at launch, so this
            // is an incremental rebuild of that one target after saving the scene.
            if (ImGui::MenuItem(building ? "Build for PC (building...)" : "Build for PC", nullptr, false, !building && sceneFile && !ctx.IsPlaying)) {
                if (SaveScene(ctx))
                    BuildPipeline::BuildForPCAsync(ctx.BuildDirectory);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4"))
                ctx.Session.RequestQuit = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            const bool canUndo = !ctx.IsPlaying && ctx.History.CanUndo();
            const bool canRedo = !ctx.IsPlaying && ctx.History.CanRedo();
            const std::string undoLabel = WithIcon(EditorIcons::Undo, canUndo ? ("Undo " + ctx.History.UndoLabel()).c_str() : "Undo");
            const std::string redoLabel = WithIcon(EditorIcons::Redo, canRedo ? ("Redo " + ctx.History.RedoLabel()).c_str() : "Redo");
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo))
                UndoScene(ctx);
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, canRedo))
                RedoScene(ctx);
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A"))
                EditorCommands::SelectAll(ctx);
            if (ImGui::MenuItem("Deselect All", nullptr, false, !ctx.SelectedEntities.empty()))
                EditorCommands::ClearSelection(ctx);
            ImGui::Separator();
            EditorCommands::DrawEntityEditMenuItems(ctx);
            if (ImGui::MenuItem("Frame Selected", "F", false, static_cast<bool>(ctx.Selected)))
                ctx.Session.RequestFrameSelected = true;
            ImGui::Separator();
            if (ImGui::MenuItem(ctx.IsPlaying ? "Stop" : "Play", "Ctrl+P", false, !ctx.IsEditingPrefab))
                EditorCommands::TogglePlay(ctx);
            if (ImGui::MenuItem("Pause", "Ctrl+Shift+P", ctx.Session.IsPaused))
                EditorCommands::TogglePause(ctx);
            if (ImGui::MenuItem("Step", "Ctrl+Alt+P", false, !ctx.IsEditingPrefab))
                EditorCommands::Step(ctx);
            ImGui::Separator();
            ImGui::MenuItem(WithIcon(EditorIcons::Settings, "Project Settings...").c_str(), nullptr, &ctx.ShowProjectSettings);
            ImGui::MenuItem(WithIcon(EditorIcons::Sliders, "Preferences...").c_str(), nullptr, &ctx.ShowPreferences);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject")) {
            EditorCommands::DrawCreateObjectMenu(ctx);
            ImGui::Separator();
            if (ImGui::MenuItem("Toggle Active State", "Alt+Shift+A", false, static_cast<bool>(ctx.Selected)))
                EditorCommands::ToggleActiveSelection(ctx);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Component")) {
            const bool hasSelection = !ctx.SelectedEntities.empty();
            if (!hasSelection)
                ImGui::TextDisabled("Select a GameObject first");
            for (const char* category : EditorCommands::ComponentCategories()) {
                if (!ImGui::BeginMenu(category, hasSelection))
                    continue;
                for (ComponentTypeInfo& type : TypeRegistry::All()) {
                    if (type.Category.empty() ||
                        std::string(EditorCommands::ComponentCategoryOf(type.DisplayName)) != category)
                        continue;
                    if (ImGui::MenuItem(type.DisplayName.c_str()))
                        EditorCommands::AddComponentToSelection(ctx, type);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            if (ImGui::BeginMenu("General")) {
                ImGui::MenuItem("Scene", nullptr, &ctx.Session.ShowScene);
                ImGui::MenuItem("Game", nullptr, &ctx.Session.ShowGame);
                ImGui::MenuItem("Inspector", nullptr, &ctx.Session.ShowInspector);
                ImGui::MenuItem("Hierarchy", nullptr, &ctx.Session.ShowHierarchy);
                ImGui::MenuItem("Project", nullptr, &ctx.Session.ShowProject);
                ImGui::MenuItem("Console", nullptr, &ctx.Session.ShowConsole);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Rendering")) {
                // Unity: Window > Rendering > Lighting (scene ambient, lights, shadows).
                ImGui::MenuItem("Lighting", nullptr, &LightingPanel::IsOpen());
                ImGui::EndMenu();
            }
            // Unity: Window > Animation > Animation (Ctrl+6), Window > 2D > Sprite Editor /
            // Tile Palette, Window > Analysis > Profiler (Ctrl+7).
            if (ImGui::BeginMenu("Animation")) {
                ImGui::MenuItem("Animation", "Ctrl+6", &AnimationWindow::IsOpen());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("2D")) {
                if (ImGui::MenuItem("Sprite Editor", nullptr, SpriteEditorWindow::IsOpen()))
                    SpriteEditorWindow::IsOpen() = !SpriteEditorWindow::IsOpen();
                ImGui::MenuItem("Tile Palette", nullptr, &TilePaletteWindow::IsOpen());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Analysis")) {
                ImGui::MenuItem("Profiler", "Ctrl+7", &ProfilerWindow::IsOpen());
                ImGui::EndMenu();
            }
            ImGui::MenuItem(WithIcon(EditorIcons::Package, "Package Manager").c_str(), nullptr, &ctx.ShowPackageManager);
            ImGui::Separator();
            if (ImGui::BeginMenu("Layouts")) {
                if (ImGui::MenuItem("Reset All Layouts (Default)"))
                    ctx.Session.RequestResetLayout = true;
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::TextDisabled("DualityEngine Editor");
            ImGui::Separator();
            ImGui::TextUnformatted("Docs: Docs/pages/editor.html (shortcuts table)");
            ImGui::EndMenu();
        }

        // Open scene / prefab name on the right side of the menu bar, '*' while unsaved.
        std::string sceneLabel = ctx.IsEditingPrefab
            ? ("Prefab Mode: " + std::filesystem::path(ctx.EditingPrefabPath).filename().string())
            : (ctx.ScenePath.empty() ? std::string("Untitled") : std::filesystem::path(ctx.ScenePath).filename().string());
        if (ctx.SceneDirty)
            sceneLabel += "*";
        const float exitWidth = ctx.IsEditingPrefab ? ImGui::CalcTextSize("< Exit Prefab Mode").x + ImGui::GetStyle().FramePadding.x * 4.0f : 0.0f;
        const float labelWidth = ImGui::CalcTextSize(sceneLabel.c_str()).x + exitWidth + 24.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - labelWidth));
        ImGui::TextDisabled("%s", sceneLabel.c_str());
        if (ctx.IsEditingPrefab) {
            ImGui::SameLine();
            if (ImGui::SmallButton("< Exit Prefab Mode"))
                ctx.RequestExitPrefabMode = true;
        }

        ImGui::EndMenuBar();
    }

}
