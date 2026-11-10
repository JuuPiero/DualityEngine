#include "DualityEditor/SceneOps.h"

#include "DualityEditor/EditorCommands.h"
#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/SceneValidator.h"

namespace Duality {

    void MarkSceneDirty(EditorContext& ctx, const char* undoLabel) {
        if (ctx.IsPlaying)
            return;
        ctx.SceneDirty = true;
        ++ctx.SceneChangeSerial;
        EditorSession& session = ctx.Session;
        if (!session.ChangeGroupOpen) {
            session.ChangeGroupOpen = true;
            session.SelectionBeforeChange = session.FrameStartSelection;
        }
        if (session.PendingUndoLabel.empty() && undoLabel && undoLabel[0] != '\0')
            session.PendingUndoLabel = undoLabel;
    }

    bool SaveScene(EditorContext& ctx) {
        if (ctx.IsPlaying) {
            Log::Warn("Exit Play mode before saving the scene (changes made in Play mode are not kept).");
            return false;
        }
        if (ctx.IsEditingPrefab) {
            if (ctx.SceneRef.GetRootEntities().empty())
                return false;
            if (PrefabSerializer::Save(ctx.SceneRef.GetRootEntities().front(), ctx.EditingPrefabPath)) {
                ctx.History.MarkSaved(ctx.SceneRef, ctx.Session.Ids);
                ctx.SceneDirty = false;
                return true;
            }
            return false;
        }
        if (ctx.ScenePath.empty()) {
            ctx.RequestSaveSceneAs = true;
            return false;
        }
        if (!SceneSerializer(ctx.SceneRef).Serialize(ctx.ScenePath))
            return false;
        ctx.History.MarkSaved(ctx.SceneRef, ctx.Session.Ids);
        ctx.SceneDirty = false;
        return true;
    }

    namespace {
        void FinishHistoryRestore(EditorContext& ctx, const HistorySelection& selection) {
            ctx.DraggingGizmoAxis = GizmoAxis::None;
            ctx.SelectedAssetPath.clear();
            ctx.SelectedAssetPaths.clear();
            EditorCommands::ApplySelection(ctx, selection);
            ctx.SceneDirty = !ctx.History.IsAtSavedState();
            // A restore is not itself a new change.
            ctx.Session.ChangeGroupOpen = false;
            ctx.Session.PendingUndoLabel.clear();
        }
    }

    bool UndoScene(EditorContext& ctx) {
        HistorySelection selection;
        if (ctx.IsPlaying || !ctx.History.Undo(ctx.SceneRef, ctx.Session.Ids, &selection))
            return false;
        FinishHistoryRestore(ctx, selection);
        return true;
    }

    bool RedoScene(EditorContext& ctx) {
        HistorySelection selection;
        if (ctx.IsPlaying || !ctx.History.Redo(ctx.SceneRef, ctx.Session.Ids, &selection))
            return false;
        FinishHistoryRestore(ctx, selection);
        return true;
    }

    bool ValidateSceneForRuntime(EditorContext& ctx, const char* operation) {
        const SceneValidationResult validation = SceneValidator::Validate(ctx.SceneRef);
        for (const SceneValidationIssue& issue : validation.Issues) {
            const char* severity = issue.Severity == SceneValidationSeverity::Error ? "error" : "warning";
            Log::Error(std::string("Scene validation ") + severity + " [" + issue.EntityName + "]: " + issue.Message);
        }
        if (validation.HasErrors()) {
            Log::Error(std::string(operation) + " cancelled: fix Scene validation errors first.");
            return false;
        }
        return true;
    }

    namespace {
        void ResetSceneViewState(EditorContext& ctx) {
            ctx.Selected = Entity();
            ctx.SelectedEntities.clear();
            ctx.SelectedAssetPath.clear();
            ctx.SelectedAssetPaths.clear();
            ctx.DraggingGizmoAxis = GizmoAxis::None;
            ctx.TopSceneView = SceneViewCamera();
            ctx.BottomSceneView = SceneViewCamera();
            ctx.TopSceneView3D = SceneViewCamera3D();
            ctx.BottomSceneView3D = SceneViewCamera3D();
            ctx.Session.Ids.Clear();
            ctx.Session.ChangeGroupOpen = false;
            ctx.Session.PendingUndoLabel.clear();
        }
    }

    void OpenScene(EditorContext& ctx, const std::string& path) {
        if (ctx.IsPlaying)
            EditorCommands::StopPlay(ctx);

        ctx.SceneRef.Clear();
        ResetSceneViewState(ctx);
        ctx.ScenePath = path;

        // A brand new/empty scene file just logs an error and leaves the (already-cleared)
        // scene empty -- same graceful-degradation precedent as OpenProjectFromDialog's own
        // first load against a fresh project.
        SceneSerializer(ctx.SceneRef).Deserialize(path);
        ctx.History.Reset(ctx.SceneRef, ctx.Session.Ids);
        ctx.SceneDirty = false;
    }

    void OpenSceneWithPrompt(EditorContext& ctx, const std::string& path) {
        if (!ctx.Session.RunAfterSavePrompt) {
            OpenScene(ctx, path);
            return;
        }
        ctx.Session.RunAfterSavePrompt([path](EditorContext& later) { OpenScene(later, path); });
    }

    void NewSceneWithPrompt(EditorContext& ctx) {
        auto action = [](EditorContext& later) {
            if (later.IsPlaying)
                EditorCommands::StopPlay(later);
            later.SceneRef.Clear();
            ResetSceneViewState(later);
            later.ScenePath.clear();
            // Unity's default new scene carries a camera; for the dual-screen 3DS layout the
            // equivalent is one root + 2D camera per physical screen.
            Entity top = later.SceneRef.EnsureScreenRoot(Screen::Top);
            Entity bottom = later.SceneRef.EnsureScreenRoot(Screen::Bottom);
            for (Screen screen : { Screen::Top, Screen::Bottom }) {
                Entity camera = later.SceneRef.CreateEntity(screen == Screen::Top ? "Top Camera" : "Bottom Camera");
                camera.AddComponent<CameraComponent>().Screen = screen;
                camera.AddComponent<PhysicsRaycaster2DComponent>();
                later.SceneRef.SetParent(camera, screen == Screen::Top ? top : bottom);
            }
            later.History.Reset(later.SceneRef, later.Session.Ids);
            later.SceneDirty = false;
        };
        if (ctx.IsEditingPrefab)
            return;
        if (!ctx.Session.RunAfterSavePrompt) {
            action(ctx);
            return;
        }
        ctx.Session.RunAfterSavePrompt(action);
    }

}
