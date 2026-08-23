#pragma once

#include <string>

#include <glm/glm.hpp>

#include "DualityEditor/Framebuffer.h"
#include "DualityEditor/SceneGizmo.h"
#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer2D.h"
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

        glm::vec2& SceneCameraPos;
        float& SceneZoom;
        GizmoMode& ActiveGizmoMode;
        GizmoAxis& DraggingGizmoAxis;

        Framebuffer& SceneFramebuffer;
        Framebuffer& TopFramebuffer;
        Framebuffer& BottomFramebuffer;
        OpenGLRenderer2D& Renderer;

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
