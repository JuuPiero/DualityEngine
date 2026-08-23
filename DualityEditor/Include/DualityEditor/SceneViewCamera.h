#pragma once

#include <glm/glm.hpp>

namespace Duality {

    // Per-pane free-roam edit camera for the split Scene view (one instance per
    // screen -- see EditorContext::TopSceneView/BottomSceneView). Deliberately
    // separate from CameraComponent: panning/zooming while placing sprites should
    // never mutate the actual gameplay camera, matching Unity's Scene-vs-Game
    // separation. Seeded once from that screen's real primary CameraComponent (if
    // any) so the initial view still looks WYSIWYG -- see ScenePanel.cpp.
    struct SceneViewCamera {
        glm::vec2 Position{ 0.0f, 0.0f };
        float Zoom = 1.0f;
        bool Seeded = false;
    };

}
