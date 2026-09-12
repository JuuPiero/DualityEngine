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

    // World-unit tuning, matching Unity Rigidbody2D. With gravity 9.81 units/s^2, a 4.5 unit/s
    // jump reaches about 1.03 world units (103 px at the default PPU 100).
    DUALITY_PROPERTY() float MoveSpeed = 2.0f;
    DUALITY_PROPERTY() float JumpSpeed = 4.5f;
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
