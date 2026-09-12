#include "FeatureShowcaseBehaviour.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "DualityEngine/Input/KeyCode.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/AudioSource.h"
#include "DualityEngine/Scripting/EntityLayer.h"
#include "DualityEngine/Scripting/Rigidbody2D.h"
#include "DualityEngine/Scripting/ScriptDebug.h"
#include "DualityEngine/Scripting/Input.h"
#include "DualityEngine/Scripting/ScriptPhysics2D.h"
#include "DualityEngine/Scripting/SpriteRenderer.h"
#include "DualityEngine/Scripting/Transform.h"
#include "ScriptRegistration.h"

namespace {
    constexpr const char* BeepSoundGuid = "8a4d16acd221a915c511d03ab1e78b16";
}

void FeatureShowcaseBehaviour::OnCreate() {
    m_BaseY = Duality::Transform(GetEntity()).GetLocalPosition().y;

    if (RenderLayer != Duality::Layer::Default)
        Duality::EntityLayer(GetEntity()).SetComponentLayer(RenderLayer);

    if (Tuning.VerboseLog && !m_LoggedCreate) {
        Duality::ScriptDebug::LogInfo("[FeatureShowcase] OnCreate -- Motion="
            + std::to_string(static_cast<int>(Motion)) + ", layer=" + Duality::LayerName(RenderLayer));
        m_LoggedCreate = true;
    }
}

void FeatureShowcaseBehaviour::OnUpdate(float deltaTime) {
    m_Time += deltaTime;
    Duality::Transform transform(GetEntity());
    glm::vec3 position = transform.GetLocalPosition();

    float horizontal = Duality::Input::GetAxis("Horizontal");
    float vertical = Duality::Input::GetAxis("Vertical");
    position.x += horizontal * MoveSpeed * Tuning.Intensity * deltaTime;
    position.y += vertical * MoveSpeed * Tuning.Intensity * deltaTime;

    switch (Motion) {
        case ShowcaseMotion::Idle:
            break;
        case ShowcaseMotion::Bounce:
            position.y = m_BaseY + std::sin(m_Time * 4.0f * Tuning.Intensity) * 0.24f;
            break;
        case ShowcaseMotion::Spin:
            transform.SetLocalRotation({ 0.0f, 0.0f, m_Time * 90.0f * Tuning.Intensity });
            break;
        case ShowcaseMotion::FollowTarget: {
            Duality::Entity target = ResolveEntityRef(LookAtTarget);
            if (target) {
                glm::vec3 targetPos = target.GetComponent<Duality::TransformComponent>().Translation;
                glm::vec2 toTarget = glm::vec2(targetPos.x - position.x, targetPos.y - position.y);
                float distance = glm::length(toTarget);
                if (distance > 1.0f) {
                    glm::vec2 step = (toTarget / distance) * std::min(distance, MoveSpeed * Tuning.Intensity * deltaTime);
                    position.x += step.x;
                    position.y += step.y;
                }
            }
            break;
        }
    }

    if (Motion != ShowcaseMotion::Spin)
        transform.SetLocalPosition(position);

    if (EnablePhysicsPush && GetEntity().HasComponent<Duality::Rigidbody2DComponent>()) {
        Duality::Rigidbody2D rb(GetEntity());
        if (PreferredBody == Duality::BodyType::Dynamic)
            rb.AddForce({ horizontal * 120.0f * Tuning.Intensity, vertical * 120.0f * Tuning.Intensity });
    }

    Duality::SpriteRenderer spriteRenderer(GetEntity());
    if (spriteRenderer) {
        float hue = static_cast<float>(static_cast<int>(Motion)) / 4.0f;
        spriteRenderer.SetColor({ hue, 0.7f, 1.0f - hue, 1.0f });
    }

    if (Duality::Input::GetPointerDown()) {
        glm::vec2 pointer = Duality::Input::GetPointerPosition();
        glm::vec2 origin = { position.x, position.y };
        glm::vec2 dir = pointer - origin;
        float len = glm::length(dir);
        if (len > 0.01f) {
            dir /= len;
            auto hit = Duality::ScriptPhysics2D::Raycast(origin, dir, 5.12f);
            if (hit && Tuning.VerboseLog)
                Duality::ScriptDebug::LogInfo("[FeatureShowcase] Raycast hit entity");
        }
    }

    if (Duality::Input::GetKeyDown(Duality::KeyCode::Space) || Duality::Input::GetKeyDown(Duality::KeyCode::GamepadA)) {
        Duality::AudioSource audio(GetEntity());
        if (!GetEntity().HasComponent<Duality::AudioSourceComponent>()) {
            auto& source = GetEntity().AddComponent<Duality::AudioSourceComponent>();
            source.Clip.Guid = SfxClip.Guid.empty() ? BeepSoundGuid : SfxClip.Guid;
        } else if (GetEntity().GetComponent<Duality::AudioSourceComponent>().Clip.Guid.empty()) {
            GetEntity().GetComponent<Duality::AudioSourceComponent>().Clip.Guid =
                SfxClip.Guid.empty() ? BeepSoundGuid : SfxClip.Guid;
        }
        audio.Play();
    }
}

REGISTER_BEHAVIOUR(FeatureShowcaseBehaviour)
