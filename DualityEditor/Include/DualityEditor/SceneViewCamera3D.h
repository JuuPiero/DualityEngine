#pragma once

#include <glm/glm.hpp>

namespace Duality {

    // Per-pane free-roam orbit camera for the split Scene view's 3D mode (one instance per
    // screen -- see EditorContext::TopSceneView3D/BottomSceneView3D), the 3D analog of
    // SceneViewCamera. Deliberately separate from CameraComponent for the same reason
    // SceneViewCamera is: panning/orbiting while placing meshes should never mutate the actual
    // gameplay camera. Seeded once from that screen's real primary CameraComponent (if any and
    // if Perspective) so the initial view still looks WYSIWYG -- see ScenePanel.cpp.
    struct SceneViewCamera3D {
        glm::vec3 Target{ 0.0f, 0.0f, 0.0f };
        float Yaw = 0.0f;
        float Pitch = 20.0f;
        float Distance = 300.0f;
        bool Seeded = false;
    };

}
