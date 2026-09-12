#include "ApiShowcaseBehaviour.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "DualityEngine/Core/DateTime.h"
#include "DualityEngine/IO/SaveSystem.h"
#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/AudioSource.h"
#include "DualityEngine/Scripting/Input.h"
#include "ScriptRegistration.h"

namespace {
    constexpr const char* SaveFilePath = "Saves/demo_save.json";

    // SampleProject/Assets/Audio/beep.wav's .meta guid (see
    // DualityEngine/Asset/AssetMeta.h) -- hardcoded since a script has no
    // "find asset by path" API, only AssetRef-by-guid (Reflection/Field.h).
    constexpr const char* BeepSoundGuid = "8a4d16acd221a915c511d03ab1e78b16";

    constexpr float MoveSpeed = 0.8f;
    constexpr float PointerFollowSpeed = 1.2f;
}

void ApiShowcaseBehaviour::OnCreate() {
    // SaveSystem/DateTime-driven hue recolor only makes sense for a sprite -- Color is a
    // per-entity SpriteRendererComponent field, whereas a 3D mesh's color lives in its shared
    // Material asset (see DualityEngine/Asset/Material.h), which other entities may reference
    // too, so a script mutating it here would recolor everything sharing that asset, not just
    // this one. This also keeps the 3D showcase entity (see SetupDemoScene, same script class)
    // from double-incrementing this same save file's playCount.
    if (!GetEntity().HasComponent<Duality::SpriteRendererComponent>())
        return;

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
    bool is3D = GetEntity().HasComponent<Duality::MeshRendererComponent>();

    // Input (axes): digital +-1 from WASD/arrows on desktop, real analog values from the
    // Circle Pad on 3DS -- same script code either way. A 3D mesh entity moves in the ground
    // plane (X/Z, this engine's 3D convention -- see IRenderer3D.h/ScenePanel.cpp's orbit
    // camera) instead of 2D's screen-space X/Y.
    float horizontal = Duality::Input::GetAxis("Horizontal");
    float vertical = Duality::Input::GetAxis("Vertical");
    transform.Translation.x += horizontal * MoveSpeed * deltaTime;
    if (is3D)
        transform.Translation.z += vertical * MoveSpeed * deltaTime;
    else
        transform.Translation.y += vertical * MoveSpeed * deltaTime;

    // Input (pointer): mouse on desktop, touch on 3DS -- sprite-only. This is a rough demo of
    // the API, not a polished feature -- GetPointerPosition() returns raw window/touchscreen
    // pixel coordinates, not this entity's own world space, so "follow" here is only
    // approximate even in 2D; there's no script-facing screen-to-3D-world-ray API yet to make
    // an equivalent meaningful for a mesh entity (see ScenePanel.cpp's own Editor-only pick
    // ray for what that would need).
    if (!is3D && Duality::Input::GetPointerDown()) {
        glm::vec2 pointer = Duality::Input::GetPointerPosition();
        glm::vec2 toPointer = pointer - glm::vec2(transform.Translation.x, transform.Translation.y);
        float distance = glm::length(toPointer);
        if (distance > 1.0f) {
            glm::vec2 step = (toPointer / distance) * std::min(distance, PointerFollowSpeed * deltaTime);
            transform.Translation.x += step.x;
            transform.Translation.y += step.y;
        }
    }

    // Input (keys) -> AudioSource component: a short beep on Space (desktop) or A (3DS).
    if (Duality::Input::GetKeyDown(Duality::KeyCode::Space) || Duality::Input::GetKeyDown(Duality::KeyCode::GamepadA)) {
        if (!GetEntity().HasComponent<Duality::AudioSourceComponent>()) {
            auto& audio = GetEntity().AddComponent<Duality::AudioSourceComponent>();
            audio.Clip.Guid = BeepSoundGuid;
        } else if (GetComponent<Duality::AudioSourceComponent>().Clip.Guid.empty()) {
            GetComponent<Duality::AudioSourceComponent>().Clip.Guid = BeepSoundGuid;
        }
        Duality::AudioSource(GetEntity()).Play();
    }

    // DateTime: real-world clock drives rotation continuously, visible proof it's live even
    // with no input at all. A sprite spins around Z (the screen-facing axis); a mesh spins
    // around Y (yaw) instead -- the natural "turntable" axis for something viewed from an
    // arbitrary 3D angle, rather than a Z-spin that would just roll it sideways.
    float degrees = static_cast<float>(Duality::DateTime::Now().Second) * 6.0f; // 6 degrees/second-of-minute = one full turn per minute
    if (is3D)
        transform.Rotation.y = degrees;
    else
        transform.Rotation.z = degrees;
}

REGISTER_BEHAVIOUR(ApiShowcaseBehaviour)
