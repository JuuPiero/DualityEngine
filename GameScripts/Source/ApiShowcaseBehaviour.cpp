#include "ApiShowcaseBehaviour.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "DualityEngine/Core/DateTime.h"
#include "DualityEngine/IO/SaveSystem.h"
#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Scene/Components.h"
#include "ScriptRegistration.h"

namespace {
    constexpr const char* SaveFilePath = "Saves/demo_save.json";

    // SampleProject/Assets/Audio/beep.wav's .meta guid (see
    // DualityEngine/Asset/AssetMeta.h) -- hardcoded since a script has no
    // "find asset by path" API, only AssetRef-by-guid (Reflection/Field.h).
    constexpr const char* BeepSoundGuid = "8a4d16acd221a915c511d03ab1e78b16";

    constexpr float MoveSpeed = 80.0f;
    constexpr float PointerFollowSpeed = 120.0f;
}

void ApiShowcaseBehaviour::OnCreate() {
    // SaveSystem: load a persisted play-count, increment it, save it back --
    // proven across app relaunches, not just within one run (no Debug.Log
    // access from scripts yet, so the round-trip has to show up visually).
    nlohmann::json save = Duality::SaveSystem::LoadJson(SaveFilePath);
    m_PlayCount = save.value("playCount", 0) + 1;
    save["playCount"] = m_PlayCount;
    Duality::SaveSystem::SaveJson(SaveFilePath, save);

    float hue = static_cast<float>(m_PlayCount % 10) / 10.0f;
    GetComponent<Duality::SpriteRendererComponent>().Color = { hue, 1.0f - hue, 0.6f, 1.0f };
}

void ApiShowcaseBehaviour::OnUpdate(float deltaTime) {
    auto& transform = GetComponent<Duality::TransformComponent>();

    // Input (axes): digital +-1 from WASD/arrows on desktop, real analog
    // values from the Circle Pad on 3DS -- same script code either way.
    float horizontal = GetAxis("Horizontal");
    float vertical = GetAxis("Vertical");
    transform.Translation.x += horizontal * MoveSpeed * deltaTime;
    transform.Translation.y += vertical * MoveSpeed * deltaTime;

    // Input (pointer): mouse on desktop, touch on 3DS. This is a rough
    // demo of the API, not a polished feature -- GetPointerPosition()
    // returns raw window/touchscreen pixel coordinates, not this entity's
    // own world space, so "follow" here is only approximate.
    if (GetPointerDown()) {
        glm::vec2 pointer = GetPointerPosition();
        glm::vec2 toPointer = pointer - glm::vec2(transform.Translation.x, transform.Translation.y);
        float distance = glm::length(toPointer);
        if (distance > 1.0f) {
            glm::vec2 step = (toPointer / distance) * std::min(distance, PointerFollowSpeed * deltaTime);
            transform.Translation.x += step.x;
            transform.Translation.y += step.y;
        }
    }

    // Input (keys) -> Audio: a short beep on Space (desktop) or A (3DS).
    if (GetKeyDown(Duality::KeyCode::Space) || GetKeyDown(Duality::KeyCode::GamepadA))
        PlaySound(BeepSoundGuid);

    // DateTime: real-world clock drives rotation continuously, visible
    // proof it's live even with no input at all.
    transform.Rotation.z = static_cast<float>(Duality::DateTime::Now().Second) * 6.0f; // 6 degrees/second-of-minute = one full turn per minute
}

REGISTER_BEHAVIOUR(ApiShowcaseBehaviour)
