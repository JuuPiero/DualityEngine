#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Duality {

    // Per-pane editor camera of the Scene view's 3D mode (one per screen -- see
    // EditorContext::TopSceneView3D/BottomSceneView3D), the 3D analog of SceneViewCamera.
    // Deliberately separate from CameraComponent: navigating never moves the gameplay camera.
    // Seeded once from that screen's primary CameraComponent (if Perspective) so the first view
    // is WYSIWYG -- see ScenePanel.cpp.
    //
    // Orbit model of the PiaEngine / Unity Scene view: a pivot (Target), a Distance, Yaw about
    // world +Y and Pitch (radians, clamped to +-90 degrees). Rotation = Ry(Yaw) * Rx(Pitch) is
    // derived from them (SceneViewNavigation keeps it in sync) and is what rendering, picking
    // and gizmos read: forward = Rotation * (0,0,-1).
    struct SceneViewCamera3D {
        glm::vec3 Target{ 0.0f, 0.0f, 0.0f };
        float Yaw = 0.0f;
        float Pitch = glm::radians(20.0f);
        glm::quat Rotation = glm::angleAxis(glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        // 2D assets use world units (typically 1-3 units at PPU 100), so a large startup
        // distance made every SpriteRenderer invisible in Scene 3D. F frames any selection.
        float Distance = 10.0f;
        bool Orthographic = false;
        // Fly mode (RMB + WASD/QE): speed multiplier, changed by the wheel while flying.
        float FlySpeed = 1.0f;
        bool Seeded = false;

        // Frame Selected / axis-snap animation (0.25 s ease-out); Anim < 0 = idle.
        float Anim = -1.0f;
        glm::vec3 AnimFromTarget{ 0.0f }, AnimToTarget{ 0.0f };
        float AnimFromDistance = 0.0f, AnimToDistance = 0.0f;
        float AnimFromYaw = 0.0f, AnimToYaw = 0.0f, AnimFromPitch = 0.0f, AnimToPitch = 0.0f;

        glm::vec3 Forward() const { return Rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
        glm::vec3 Right() const { return Rotation * glm::vec3(1.0f, 0.0f, 0.0f); }
        glm::vec3 Up() const { return Rotation * glm::vec3(0.0f, 1.0f, 0.0f); }
        glm::vec3 Position() const { return Target - Forward() * Distance; }
        void SyncRotation() {
            Rotation = glm::normalize(glm::angleAxis(Yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(Pitch, glm::vec3(1.0f, 0.0f, 0.0f)));
        }
    };

}
