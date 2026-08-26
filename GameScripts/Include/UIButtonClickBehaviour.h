#pragma once

#include "DualityEngine/Scene/Behaviour.h"

// Logs a line to the Console whenever this entity's own UIButtonComponent is clicked -- meant
// to be attached via a UIDocument markup element's `behaviour="UIButtonClickBehaviour"`
// attribute, demonstrating the declarative-markup-to-script wiring UIDocument supports (see its
// own header comment on why this is a real Behaviour polling WasClicked, not an XML-declared
// callback name).
class UIButtonClickBehaviour : public Duality::Behaviour {
public:
    void OnUpdate(float deltaTime) override;
};
