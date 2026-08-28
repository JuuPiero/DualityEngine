#include "UIDocumentDemoBehaviour.h"

#include "DualityEngine/Scripting/ScriptDebug.h"
#include "DualityEngine/Scripting/ScriptScene.h"
#include "ScriptRegistration.h"

void UIDocumentDemoBehaviour::OnCreate() {
    if (MenuDocument.Guid.empty()) {
        Duality::ScriptDebug::LogWarn("UIDocumentDemoBehaviour: no MenuDocument assigned");
        return;
    }
    Duality::Entity menu = Duality::ScriptScene::InstantiateUIDocument(MenuDocument.Guid, Duality::Screen::Bottom);
    if (!menu)
        Duality::ScriptDebug::LogWarn("UIDocumentDemoBehaviour: MenuDocument failed to load/parse -- check the Console for the reason");
}

REGISTER_BEHAVIOUR(UIDocumentDemoBehaviour)
