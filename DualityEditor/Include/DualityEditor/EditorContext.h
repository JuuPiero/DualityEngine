#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "DualityEditor/Framebuffer.h"
#include "DualityEditor/SceneGizmo.h"
#include "DualityEditor/SceneViewCamera.h"
#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

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

        Framebuffer& TopSceneFramebuffer;
        Framebuffer& BottomSceneFramebuffer;
        Framebuffer& TopFramebuffer;
        Framebuffer& BottomFramebuffer;
        OpenGLRenderer2D& Renderer;

        // Stats overlay (GamePanel): Fps is exponentially smoothed by Application;
        // GameDrawCallCount is the real dual-screen render pass's draw call count
        // for this frame, snapshotted before the Scene view's own (Editor-only)
        // draws would otherwise inflate it -- see Application::Run().
        const float& Fps;
        const uint32_t& GameDrawCallCount;

        const std::string& ScenePath;
        const std::string& BuildDirectory;
        const std::string& RepoRoot;

        // Set by MenuBarPanel when "Open Project..." is clicked; Application
        // checks this right after the menu bar renders and, if set, shows
        // the native file dialog and swaps the active project/scene --
        // heavier than a panel should do on its own, so it's a request
        // flag rather than a callback into Application's internals.
        bool& RequestOpenProject;
    };

}
