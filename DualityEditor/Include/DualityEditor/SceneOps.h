#pragma once

#include <string>

namespace Duality {

    struct EditorContext;

    // Marks the active scene as having unsaved edits (shown as * in the title / Scene tab) and
    // opens an Undo group labelled `undoLabel` ("Move", "Add Component"...). Every mutation of
    // the same interaction (one drag, one typed value, one menu command) is recorded as ONE
    // Undo step: Application commits the group once no widget / gizmo is still being dragged.
    // Skipped while Play is running -- runtime tweaks revert on Stop and shouldn't prompt a save
    // of transient state.
    void MarkSceneDirty(EditorContext& ctx, const char* undoLabel = nullptr);

    // Writes ctx.ScenePath (or the prefab being edited) and clears the dirty flag. An untitled
    // scene (empty ScenePath) asks for a file name instead (Save As). Returns false if nothing
    // was written.
    bool SaveScene(EditorContext& ctx);

    // Restores the preceding/following Editor scene snapshot. These are deliberately Scene
    // operations (not file-system operations): Ctrl+Z while editing a text field is still owned
    // by ImGui's text widget, while Ctrl+Z elsewhere invokes these helpers. Selection is
    // restored to what it was around that change (by stable entity id).
    bool UndoScene(EditorContext& ctx);
    bool RedoScene(EditorContext& ctx);

    // Runs authored-scene checks before Play or Build. Diagnostics are written
    // to the Console panel; returns false when an error makes the operation
    // unsafe to start.
    bool ValidateSceneForRuntime(EditorContext& ctx, const char* operation);

    // Stops Play if running, clears ctx.SceneRef (Scene::Clear()), and loads `path` into it
    // as the new active scene -- lands in Edit mode regardless of whether Play was running.
    // Shared by MenuBarPanel's "Open Scene...", ContentBrowserPanel's double-click-to-open, and
    // the Scene asset inspector's "Open Scene" button, so the stop-play-first + clear-before-load
    // invariants live in exactly one place. Also re-seeds the Scene view's free-roam cameras and
    // resets the selection. Does NOT ask about unsaved changes -- use OpenSceneWithPrompt from UI.
    //
    // Deliberately NOT used by Scene::OnRuntimeUpdate's own SceneManager.LoadScene handling
    // (Application.cpp) -- that's a same-Play-session scene-to-scene transition.
    void OpenScene(EditorContext& ctx, const std::string& path);

    // OpenScene behind Unity's "Do you want to save the changes you made in <Scene>?" prompt.
    void OpenSceneWithPrompt(EditorContext& ctx, const std::string& path);

    // File > New Scene: an empty untitled scene (Save asks for a file name), behind the prompt.
    void NewSceneWithPrompt(EditorContext& ctx);

}
