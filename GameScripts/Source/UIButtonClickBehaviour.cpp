#include "UIButtonClickBehaviour.h"

#include "DualityEngine/Scene/Components.h"
#include "ScriptRegistration.h"

void UIButtonClickBehaviour::OnUpdate(float deltaTime) {
    if (GetComponent<Duality::UIButtonComponent>().WasClicked)
        LogInfo("UIButtonClickBehaviour: '" + GetComponent<Duality::NameComponent>().Name + "' was clicked");
}

REGISTER_BEHAVIOUR(UIButtonClickBehaviour)
