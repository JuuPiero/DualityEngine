#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "DualityEditor/Framebuffer.h"
#include "DualityEditor/SceneGizmo.h"
#include "DualityEditor/SceneViewCamera.h"
#include "DualityEditor/SceneViewCamera3D.h"
#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer3D.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Per-pane 2D/3D switch for the Scene view (see ScenePanel.cpp's toggle buttons) -- a pane
    // renders through exactly one pipeline at a time, mirroring CameraComponent::Projection's
    // own exclusive-switch convention for the real Game view/device screens.
    enum class RenderMode { Mode2D, Mode3D };

    // The editor's shared mutable state, threaded by reference into every
    // panel's OnImGuiRender instead of each panel taking ~10 individual
    // parameters. Every field is itself a reference onto the real value
    // owned by Application -- EditorContext is reconstructed fresh each
    // frame (cheap, just a bundle of references), but a panel writing
    // through e.g. `ctx.Selected = hit` mutates Application's own member
    // directly, with no separate write-back step needed at end of frame.
    struct EditorContext {
        Scene& SceneRef;
        Entity& Selected;
        // The currently-selected project asset file (Content Browser), if any -- e.g. a
        // ScriptableObject ".asset" the Properties panel should inspect instead of an
        // Entity's components. Mutually exclusive with Selected in practice: ContentBrowserPanel
        // clears Selected when a file is clicked; PropertiesPanel prioritizes Selected when both
        // happen to be set, so no other entity-select call site needs to clear this in turn.
        std::string& SelectedAssetPath;
        bool& IsPlaying;

        // Split Scene view: one free-roam edit camera per screen (see
        // SceneViewCamera.h) -- replaces the old single SceneCameraPos/SceneZoom
        // pair now that ScenePanel renders two side-by-side panes.
        SceneViewCamera& TopSceneView;
        SceneViewCamera& BottomSceneView;
        GizmoMode& ActiveGizmoMode;
        GizmoAxis& DraggingGizmoAxis;
        // Which pane's camera math a drag-in-progress should keep using, set once
        // when the drag starts -- so a fast mouse movement into the other pane
        // mid-drag doesn't reinterpret the drag with the wrong pane's zoom/pan.
        Screen& DraggingGizmoScreen;

        // Per-pane 2D/3D switch (see RenderMode above) and each pane's own 3D orbit
        // camera -- the 3D analogs of TopSceneView/BottomSceneView above, independent
        // per pane since orbiting one screen's 3D view has no cross-pane ambiguity to
        // resolve (unlike the shared gizmo drag, which needs DraggingGizmoScreen).
        RenderMode& TopRenderMode;
        RenderMode& BottomRenderMode;
        SceneViewCamera3D& TopSceneView3D;
        SceneViewCamera3D& BottomSceneView3D;

        Framebuffer& TopSceneFramebuffer;
        Framebuffer& BottomSceneFramebuffer;
        Framebuffer& TopFramebuffer;
        Framebuffer& BottomFramebuffer;
        OpenGLRenderer2D& Renderer;
        OpenGLRenderer3D& Renderer3D;

        // Stats overlay (GamePanel): Fps is exponentially smoothed by Application;
        // GameDrawCallCount is the real dual-screen render pass's draw call count
        // for this frame, snapshotted before the Scene view's own (Editor-only)
        // draws would otherwise inflate it -- see Application::Run().
        const float& Fps;
        const uint32_t& GameDrawCallCount;

        // Which file "Save Scene"/"Load Scene" operate on -- mutable (unlike most of the
        // other plain-data fields below, this used to be const) since SceneOps.h's
        // OpenScene() updates it directly when switching to a different scene.
        std::string& ScenePath;
        const std::string& BuildDirectory;
        const std::string& RepoRoot;

        // Captured by GamePanel's "Play" button (SceneSerializer::SerializeToJson().dump())
        // right before Scene::OnRuntimeStart(), restored by "Stop" (via Scene::Clear() +
        // DeserializeFromJson) so leaving Play mode reverts every change Play made --
        // script-driven Transform edits, physics results, runtime-spawned entities --
        // matching Unity's own Play/Stop guarantee. Owned by Application (a plain string,
        // not a live nlohmann::json, so this header doesn't need that include) since
        // EditorContext itself is rebuilt fresh every frame and can't hold state across one.
        std::string& PlaySnapshot;

        // Set by MenuBarPanel when "Open Project..." is clicked; Application
        // checks this right after the menu bar renders and, if set, shows
        // the native file dialog and swaps the active project/scene --
        // heavier than a panel should do on its own, so it's a request
        // flag rather than a callback into Application's internals.
        bool& RequestOpenProject;

        // Same request-flag convention as RequestOpenProject, for "New Project..." --
        // shows a native Save dialog (reusing FileDialogs::SaveFile, same as "Save Scene
        // As...") to pick a location + name, then calls Project::New instead of Project::Load.
        bool& RequestNewProject;

        // Same request-flag convention as RequestOpenProject, for "Save Scene As..." --
        // lets a project accumulate additional scene files (e.g. for
        // Behaviour::LoadScene to target) without hand-copying JSON outside the Editor.
        bool& RequestSaveSceneAs;

        // Same request-flag convention, for "Open Scene..." -- browses to an arbitrary
        // .scene file (needs the native window handle, which only Application has) and
        // then calls SceneOps.h's OpenScene() with the result. Opening a specific known
        // path (Content Browser double-click, the Scene asset inspector's "Open Scene"
        // button, "Load Scene") doesn't need a dialog and calls OpenScene() directly instead.
        bool& RequestOpenSceneDialog;

        // Same request-flag convention as RequestOpenSceneDialog, for PreferencesPanel's
        // "Browse..." button -- also needs the native window handle, so Application handles it
        // and writes the result straight into EditorSettings (not a Scene/Project concern).
        bool& RequestBrowseExternalEditor;

        // Same request-flag convention, for ProjectSettingsPanel's Icon "Browse..." button --
        // writes the result into the active Project's own IconPath + Save() instead of
        // EditorSettings, since an icon is per-project, not per-machine.
        bool& RequestBrowseIcon;

        // Persistent open/close state for the three floating (non-docked) utility windows below
        // -- unlike the one-shot Request* flags above (consumed the instant Application::Run()
        // notices them true), these mirror IsPlaying's own convention: a reference onto an
        // Application-owned bool that a panel just checks every frame (`if (!ctx.ShowX) return;`)
        // and can toggle back off itself via its own ImGui::Begin's close button.
        bool& ShowBuildSettings;
        bool& ShowProjectSettings;
        bool& ShowPreferences;
    };

}
