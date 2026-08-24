#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Duality {

    // Per-pane free-roam orbit camera for the split Scene view's 3D mode (one instance per
    // screen -- see EditorContext::TopSceneView3D/BottomSceneView3D), the 3D analog of
    // SceneViewCamera. Deliberately separate from CameraComponent for the same reason
    // SceneViewCamera is: panning/orbiting while placing meshes should never mutate the actual
    // gameplay camera. Seeded once from that screen's real primary CameraComponent (if any and
    // if Perspective) so the initial view still looks WYSIWYG -- see ScenePanel.cpp.
    //
    // Orientation is a quaternion, not separate Pitch/Yaw floats -- a pitch/yaw pair needs
    // right = cross(forward, worldUp) to derive the camera's other two axes, which degenerates
    // (near-zero-length cross product) as forward approaches worldUp, forcing an artificial
    // +-89 degree pitch clamp to avoid it. A quaternion has no such singularity: right/up are
    // always Rotation * localAxis, well-defined at any orientation, so the orbit camera can
    // flip smoothly through the poles like Unity/Blender's Scene view instead of stopping short.
    // Default matches the old Pitch=20/Yaw=0 starting angle (used only when there's no
    // Perspective primary camera on this screen to seed from).
    struct SceneViewCamera3D {
        glm::vec3 Target{ 0.0f, 0.0f, 0.0f };
        glm::quat Rotation = glm::angleAxis(glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        float Distance = 300.0f;
        bool Seeded = false;
    };

}
