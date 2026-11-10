#pragma once

#include <string>

namespace Duality {

    struct EditorContext;

    // Remembers each scene's Scene view cameras (both panes: 2D center/zoom, 3D pivot/distance/
    // yaw/pitch/orthographic, 2D/3D mode) like PiaEngine / Unity do, in the project's
    // Library/SceneViewState.json (per-machine editor state, never part of the scene file).
    namespace SceneViewState {
        // Restores the cameras saved for ctx.ScenePath (no-op when none were saved).
        void Load(EditorContext& ctx);
        // Saves the current cameras under ctx.ScenePath.
        void Save(EditorContext& ctx);
    }

}
