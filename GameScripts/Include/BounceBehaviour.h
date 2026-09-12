#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// A small nested, serializable struct -- Unity's [System.Serializable] class-as-a-field, for
// this engine. DUALITY_SERIALIZABLE() marks the struct itself as nestable (its own
// DUALITY_PROPERTY()-marked members still use that same, unchanged marker); a
// DUALITY_PROPERTY()-marked field elsewhere whose TYPE is this struct (BounceBehaviour::Wobble
// below) shows as an expandable/foldout group in Properties, edits recursively, and round-trips
// through Save Scene/Load Scene the same way any other field does -- see Field.h's
// NestedFieldValue/MakeNestedField for the mechanism.
struct BounceWobble {
    DUALITY_PROPERTY() float Amount = 0.0f;    // 0 = no secondary wobble, the demo's original behavior unchanged
    DUALITY_PROPERTY() float Frequency = 3.0f;

    DUALITY_SERIALIZABLE()
};

// Bounces the entity up/down around its starting Y position (or, if Target is
// set, around that other entity's current Y position instead). A trivial demo
// script, but a real one -- edited here, hot-reloaded into the running
// Editor via the "Reload Scripts" button, no code changes needed to also
// compile it statically into a future device build. Also the demo script for
// DUALITY_PROPERTY (Inspector-editable public fields), EntityRef (drag-drop
// an entity from the Hierarchy onto the Target field), and DUALITY_SERIALIZABLE
// (the nested Wobble field above).
class BounceBehaviour : public Duality::Behaviour {
public:
    void OnCreate() override;
    void OnUpdate(float deltaTime) override;

    DUALITY_PROPERTY() float Amplitude = 0.4f;
    DUALITY_PROPERTY() float Speed = 8.0f;
    DUALITY_PROPERTY() Duality::EntityRef Target;
    DUALITY_PROPERTY() BounceWobble Wobble;

    DUALITY_PROPERTIES_AUTO()

private:
    float m_Time = 0.0f;
    float m_BaseY = 0.0f;
};
