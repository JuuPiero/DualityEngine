#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Components.h"

// A complete, minimal 2D platformer player controller -- the reference example for the
// Platformer.scene demo (SampleProject/Assets/Scenes/Platformer.scene): horizontal movement +
// gravity-gated jumping, a coin counter, Hazard/Enemy/Goal trigger handling, and a simple
// horizontal camera follow, all wired up through nothing but Behaviour's own public API (no
// engine changes needed for any of this).
//
// The camera/HUD labels are found BY NAME in OnCreate (ScriptScene::FindEntityInScreen) rather
// than dragged in as EntityRef fields -- deliberately: an EntityRef field always reloads as
// unset (see Reflection/Field.h's own comment on why it can't be serialized, the same problem
// HierarchyComponent::Parent solves with an index scheme this generic field type doesn't have),
// so it would silently break the moment this scene is saved and reopened. A by-name lookup
// resolved fresh every time OnCreate runs has no such gotcha -- prefer this pattern for your
// own scripts whenever a reference needs to survive a save/load round trip.
//
// Pair with a Dynamic Rigidbody2D (FixedRotation = true, so landing doesn't tip the sprite
// over) + a BoxCollider2D (used as the ground-check ray's own half-height, see IsGrounded) + a
// SpriteRenderer + a TagComponent (not strictly required, but good practice).
class PlayerController : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;
    void OnTriggerEnter(Duality::Entity other) override;

    // Tuned for a clearly-felt jump against this engine's global Box2D gravity (400 px/s^2,
    // Scene.cpp's own DefaultGravityY -- shared by every physics body in the engine, so not
    // something this one controller should touch): apex height = JumpSpeed^2 / (2*gravity), so
    // 450 clears roughly 253px of rise, comfortably above the ~50-60px platform-to-platform
    // gaps in Platformer.scene, with real margin for a satisfying, not-barely-making-it feel.
    DUALITY_PROPERTY() float MoveSpeed = 200.0f;
    DUALITY_PROPERTY() float JumpSpeed = 450.0f;
    // Names to look up in OnCreate (see the class comment above) -- change these if you rename
    // the corresponding entities in the scene. Empty CameraName disables camera-follow.
    DUALITY_PROPERTY() std::string CameraName = "MainCamera";
    DUALITY_PROPERTY() std::string ScoreLabelName = "ScoreLabel";
    DUALITY_PROPERTY() std::string StatusLabelName = "StatusLabel";
    DUALITY_PROPERTY() float CameraFollowSpeed = 4.0f; // higher = snappier, 0 = hard-snap every frame

    // On-screen touch controls (Bottom screen, UIButtonComponent entities -- see
    // Platformer.scene's "-- Touch Controls --" group) -- looked up the same by-name way as the
    // camera/HUD labels above. ADDITIVE with keyboard/gamepad, not a replacement: holding
    // LeftButton/RightButton works exactly like holding A/D, and works identically on 3DS
    // (touch) and desktop (mouse click), which is the whole point of a UIButtonComponent-based
    // control -- Input's own keyboard axis has no device-independent equivalent.
    DUALITY_PROPERTY() std::string LeftButtonName = "LeftButton";
    DUALITY_PROPERTY() std::string RightButtonName = "RightButton";
    DUALITY_PROPERTY() std::string JumpButtonName = "JumpButton";

    DUALITY_PROPERTIES_AUTO()

private:
    bool IsGrounded();
    void Respawn();
    void UpdateScoreLabel();
    void ShowStatus(const std::string& message);
    void UpdateCamera(float deltaTime);

    glm::vec3 m_SpawnPosition{ 0.0f };
    Duality::Entity m_Camera;
    Duality::Entity m_ScoreLabel;
    Duality::Entity m_StatusLabel;
    Duality::Entity m_LeftButton;
    Duality::Entity m_RightButton;
    Duality::Entity m_JumpButton;
    bool m_JumpButtonWasDown = false; // edge-detects JumpButton's own IsPressed() level state
    int m_Coins = 0;
    bool m_Finished = false; // true once the Goal is reached -- freezes input/physics response
};
