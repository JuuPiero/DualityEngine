#pragma once

#include <string>

namespace Duality {

    struct EditorContext;

    // Marks the active scene as having unsaved edits (shown as * in the menu bar).
    // Skipped while Play is running — runtime tweaks revert on Stop and shouldn't
    // prompt a save of transient state.
    void MarkSceneDirty(EditorContext& ctx);

    // Writes ctx.ScenePath and clears the dirty flag. No-op if path is empty.
    void SaveScene(EditorContext& ctx);

    // Runs authored-scene checks before Play or Build. Diagnostics are written
    // to the Console panel; returns false when an error makes the operation
    // unsafe to start.
    bool ValidateSceneForRuntime(EditorContext& ctx, const char* operation);

    // Stops Play if running, clears ctx.SceneRef (Scene::Clear()), and loads `path` into it
    // as the new active scene -- lands in Edit mode regardless of whether Play was running.
    // Shared by MenuBarPanel's "Load Scene"/"Open Scene...", ContentBrowserPanel's
    // double-click-to-open, and the Scene asset inspector's "Open Scene" button, so the
    // stop-play-first + clear-before-load invariants live in exactly one place instead of
    // being hand-copied at each call site (which is exactly how the "Load Scene doesn't
    // clear the existing scene first" bug happened -- MenuBarPanel called Deserialize
    // directly on the live scene with no clear). Also re-seeds the Scene view's free-roam
    // cameras (the old pan/zoom position means nothing for different scene content) and
    // resets the current selection (old Entity handles don't survive the swap).
    //
    // Deliberately NOT used by Scene::OnRuntimeUpdate's own SceneManager.LoadScene handling
    // (Application.cpp) -- that's a same-Play-session scene-to-scene transition (resumes
    // Play immediately afterward, invalidates renderer texture/mesh caches since the new
    // scene may reference different assets), a genuinely different operation from an
    // Editor-driven "open this scene" action.
    void OpenScene(EditorContext& ctx, const std::string& path);

}
