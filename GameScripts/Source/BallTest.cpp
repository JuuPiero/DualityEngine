#include "BallTest.h"
// #include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptDebug.h"
#include "DualityEngine/Scripting/ScriptScene.h"
#include "ScriptRegistration.h"

using namespace Duality;

void BallTest::OnCreate()
{
    ScriptDebug::LogInfo("Hello world");
    buttonEntity = ScriptScene::FindEntityInBottomScreen("Button");
    
}
const int GRAVITY = 1;
void BallTest::OnUpdate(float deltaTime)
{
    auto &transform = GetComponent<TransformComponent>();
    auto &sprite = GetComponent<SpriteRendererComponent>();
    auto& button = buttonEntity.GetComponent<UIButtonComponent>();

    if(button.IsPressed) {
        sprite.Color = glm::vec4(255, 0, 0, 255);
    }
    velocityY += GRAVITY;
    transform.Translation.y += velocityY;
    if (transform.Translation.y + sprite.Size.y >= 73)
    {
        transform.Translation.y = 73 - sprite.Size.y;
        velocityY = -velocityY * friction;
    }
}

REGISTER_BEHAVIOUR(BallTest)
