#pragma once

#include "DualityEngine/Physics/BodyType.h"
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/Layer.h"

// Live demo of recent engine/editor features in one Inspector-friendly script:
// DUALITY_PROPERTY (scalars, bool, AssetRef, EntityRef, nested struct, script enum,
// engine enum dropdowns), GetTransform()/GetRigidbody2D(), GetEntityLayer(), ScriptInput,
// ScriptPhysics2D raycast, ScriptDebug logging, and AudioSource playback.
enum class ShowcaseMotion {
    Idle,
    Bounce,
    Spin,
    FollowTarget
};

struct ShowcaseTuning {
    DUALITY_PROPERTY() float Intensity = 1.0f;
    DUALITY_PROPERTY() bool VerboseLog = false;

    DUALITY_SERIALIZABLE()
};

class FeatureShowcaseBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

    DUALITY_PROPERTY() float MoveSpeed = 60.0f;
    DUALITY_PROPERTY() bool EnablePhysicsPush = true;
    DUALITY_PROPERTY() ShowcaseMotion Motion = ShowcaseMotion::Bounce;
    DUALITY_PROPERTY() Duality::Layer RenderLayer = Duality::Layer::Default;
    DUALITY_PROPERTY() Duality::BodyType PreferredBody = Duality::BodyType::Dynamic;
    DUALITY_PROPERTY() Duality::EntityRef LookAtTarget;
    DUALITY_PROPERTY() Duality::AssetRef SfxClip;
    DUALITY_PROPERTY() ShowcaseTuning Tuning;

    DUALITY_PROPERTIES_AUTO()

private:
    float m_Time = 0.0f;
    float m_BaseY = 0.0f;
    bool m_LoggedCreate = false;
};
