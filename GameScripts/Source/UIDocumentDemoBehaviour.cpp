#include "UIDocumentDemoBehaviour.h"

#include "ScriptRegistration.h"

void UIDocumentDemoBehaviour::OnCreate() {
    if (MenuDocument.Guid.empty()) {
        LogWarn("UIDocumentDemoBehaviour: no MenuDocument assigned");
        return;
    }
    Duality::Entity menu = InstantiateUIDocument(MenuDocument.Guid, Duality::Screen::Bottom);
    if (!menu)
        LogWarn("UIDocumentDemoBehaviour: MenuDocument failed to load/parse -- check the Console for the reason");
}

REGISTER_BEHAVIOUR(UIDocumentDemoBehaviour)
