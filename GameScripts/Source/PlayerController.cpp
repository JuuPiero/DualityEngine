#include "PlayerController.h"

#include <algorithm>
#include <string>

#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Scripting/Rigidbody2D.h"
#include "DualityEngine/Scripting/Input.h"
#include "DualityEngine/Scripting/ScriptPhysics2D.h"
#include "DualityEngine/Scripting/ScriptScene.h"
#include "DualityEngine/Scripting/Transform.h"
#include "DualityEngine/Scripting/UIWidgets.h"
#include "ScriptRegistration.h"

void PlayerController::OnCreate() {
    m_SpawnPosition = Duality::Transform(GetEntity()).GetLocalPosition();

    if (!CameraName.empty())
        m_Camera = Duality::ScriptScene::FindEntityInTopScreen(CameraName);
    if (!ScoreLabelName.empty())
        m_ScoreLabel = Duality::ScriptScene::FindEntityInTopScreen(ScoreLabelName);
    if (!StatusLabelName.empty())
        m_StatusLabel = Duality::ScriptScene::FindEntityInTopScreen(StatusLabelName);
    if (!LeftButtonName.empty())
        m_LeftButton = Duality::ScriptScene::FindEntityInBottomScreen(LeftButtonName);
    if (!RightButtonName.empty())
        m_RightButton = Duality::ScriptScene::FindEntityInBottomScreen(RightButtonName);
    if (!JumpButtonName.empty())
        m_JumpButton = Duality::ScriptScene::FindEntityInBottomScreen(JumpButtonName);

    UpdateScoreLabel();
}

bool PlayerController::IsGrounded() {
    if (!GetEntity().HasComponent<Duality::BoxCollider2DComponent>())
        return false;
    // Starts at the player's own CENTER (definitely inside its own collider -- Box2D never
    // reports a hit for a shape the ray already starts inside, confirmed empirically, so no
    // separate "ignore self" check is even needed) and casts past its own feet
    // (BoxCollider2DComponent::Size is half-extents, matching every other Box collider in this
    // engine, so Size.y is exactly the center-to-feet distance) by a small margin. An EARLIER
    // version of this started the ray just past the player's own bottom edge instead, to dodge
    // a *different* self-intersection worry -- that overshot the OTHER way while resting exactly
    // on a platform (the small settle gap is much less than 1px), landing the ray origin already
    // *inside* the ground fixture, where Box2D also reports no hit -- confirmed as the actual
    // cause of jump silently never working: a real Scene::Raycast2D probe from a resting
    // position found nothing at all. Casting from the center avoids both failure modes at once.
    auto& collider = GetEntity().GetComponent<Duality::BoxCollider2DComponent>();
    glm::vec3 position = Duality::Transform(GetEntity()).GetLocalPosition();
    glm::vec2 origin{ position.x, position.y };
    Duality::RaycastHit2D hit = Duality::ScriptPhysics2D::Raycast(origin, { 0.0f, 1.0f }, collider.Size.y + 0.04f);
    return static_cast<bool>(hit) && hit.HitEntity != GetEntity();
}

void PlayerController::OnUpdate(float deltaTime) {
    UpdateCamera(deltaTime);

    if (m_Finished)
        return;

    // Touch buttons are ADDITIVE with keyboard/gamepad -- holding LeftButton/RightButton (mouse
    // click on desktop, touch on 3DS) works exactly like holding A/D.
    float horizontal = Duality::Input::GetAxis("Horizontal");
    Duality::UIButton leftButton(m_LeftButton);
    Duality::UIButton rightButton(m_RightButton);
    if (leftButton && leftButton.IsPressed())
        horizontal -= 1.0f;
    if (rightButton && rightButton.IsPressed())
        horizontal += 1.0f;
    horizontal = std::clamp(horizontal, -1.0f, 1.0f);

    Duality::Rigidbody2D rigidbody(GetEntity());
    glm::vec2 velocity = rigidbody.GetVelocity();
    velocity.x = horizontal * MoveSpeed;

    bool grounded = IsGrounded();
    bool jumpKeyPressed = Duality::Input::GetKeyDown(Duality::KeyCode::Space)
        || Duality::Input::GetKeyDown(Duality::KeyCode::Up)
        || Duality::Input::GetKeyDown(Duality::KeyCode::GamepadA)
        || Duality::Input::GetKeyDown(Duality::KeyCode::GamepadB);
    // JumpButton only exposes a level (IsPressed), not an edge like GetKeyDown -- track the
    // transition ourselves so holding it down doesn't repeatedly re-trigger the jump velocity
    // every single frame.
    Duality::UIButton jumpButton(m_JumpButton);
    bool jumpButtonDown = jumpButton && jumpButton.IsPressed();
    bool jumpButtonPressed = jumpButtonDown && !m_JumpButtonWasDown;
    m_JumpButtonWasDown = jumpButtonDown;
    bool jumpPressed = jumpKeyPressed || jumpButtonPressed;
    if (grounded && jumpPressed)
        velocity.y = -JumpSpeed; // world +Y is down in this engine, so "up" is negative Y

    rigidbody.SetVelocity(velocity);

    if (horizontal != 0.0f && GetEntity().HasComponent<Duality::SpriteRendererComponent>())
        GetEntity().GetComponent<Duality::SpriteRendererComponent>().FlipX = horizontal < 0.0f;

    // Falling off the bottom of the level respawns at the start, same outcome as touching a
    // Hazard trigger.
    if (Duality::Transform(GetEntity()).GetLocalPosition().y > 4.0f)
        Respawn();
}

void PlayerController::UpdateCamera(float deltaTime) {
    if (!m_Camera || !m_Camera.HasComponent<Duality::TransformComponent>())
        return;
    auto& cameraTransform = m_Camera.GetComponent<Duality::TransformComponent>();
    float targetX = Duality::Transform(GetEntity()).GetLocalPosition().x;
    if (CameraFollowSpeed <= 0.0f) {
        cameraTransform.Translation.x = targetX;
    } else {
        float t = std::min(1.0f, CameraFollowSpeed * deltaTime);
        cameraTransform.Translation.x += (targetX - cameraTransform.Translation.x) * t;
    }
}

void PlayerController::OnTriggerEnter(Duality::Entity other) {
    if (m_Finished || !other.HasComponent<Duality::TagComponent>())
        return;

    const std::string& tag = other.GetComponent<Duality::TagComponent>().Tag;
    if (tag == "Coin") {
        other.GetComponent<Duality::ActiveComponent>().Active = false;
        m_Coins++;
        UpdateScoreLabel();
    } else if (tag == "Hazard" || tag == "Enemy") {
        Respawn();
    } else if (tag == "Goal") {
        m_Finished = true;
        Duality::Rigidbody2D(GetEntity()).SetVelocity({ 0.0f, 0.0f });
        ShowStatus("You Win!  Coins: " + std::to_string(m_Coins));
    }
}

void PlayerController::Respawn() {
    Duality::Transform(GetEntity()).SetLocalPosition(m_SpawnPosition);
    Duality::Rigidbody2D(GetEntity()).SetVelocity({ 0.0f, 0.0f });
}

void PlayerController::UpdateScoreLabel() {
    if (m_ScoreLabel)
        Duality::UIText(m_ScoreLabel).SetText("Coins: " + std::to_string(m_Coins));
}

void PlayerController::ShowStatus(const std::string& message) {
    if (!m_StatusLabel)
        return;
    Duality::UIText(m_StatusLabel).SetText(message);
    if (m_StatusLabel.HasComponent<Duality::UIRectComponent>())
        m_StatusLabel.GetComponent<Duality::UIRectComponent>().Enabled = true;
}

REGISTER_BEHAVIOUR(PlayerController)
