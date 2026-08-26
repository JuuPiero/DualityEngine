#include "BallTest.h"
// #include "DualityEngine/Scene/Components.h"
#include "ScriptRegistration.h"

using namespace Duality;

void BallTest::OnCreate()
{
    LogInfo("Hello world");
    buttonEntity = FindEntityInBottomScreen("Button");
    
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