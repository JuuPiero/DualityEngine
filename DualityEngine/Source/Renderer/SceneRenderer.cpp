#include "DualityEngine/Renderer/SceneRenderer.h"

#include "DualityEngine/Scene/Components.h"

namespace Duality {

    static void ScreenExtents(Screen screen, float& outWidth, float& outHeight) {
        if (screen == Screen::Top) {
            outWidth = static_cast<float>(TopScreenWidth);
            outHeight = static_cast<float>(TopScreenHeight);
        } else {
            outWidth = static_cast<float>(BottomScreenWidth);
            outHeight = static_cast<float>(BottomScreenHeight);
        }
    }

    void RenderScreen(IRenderer2D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor) {
        renderer.BeginScene(screen, clearColor);

        Entity camera = scene.GetPrimaryCamera(screen);
        if (camera) {
            auto& cameraTransform = camera.GetComponent<TransformComponent>();
            auto& cameraComponent = camera.GetComponent<CameraComponent>();
            float screenWidth, screenHeight;
            ScreenExtents(screen, screenWidth, screenHeight);

            // Every sprite in the scene is a candidate for this screen --
            // whether it ends up visible depends purely on where it sits
            // relative to this camera (a sprite far from every active
            // camera just draws off the fixed 400x240/320x240 target,
            // which is effectively culling by construction). This is real
            // camera-relative composition, not "only the camera's own
            // sprite" like the very first version of this function.
            auto view = scene.Registry().view<TransformComponent, SpriteRendererComponent>();
            for (auto handle : view) {
                auto& transform = view.get<TransformComponent>(handle);
                auto& sprite = view.get<SpriteRendererComponent>(handle);

                glm::vec2 screenCenter{
                    (transform.Translation.x - cameraTransform.Translation.x) * cameraComponent.Zoom + screenWidth * 0.5f,
                    (transform.Translation.y - cameraTransform.Translation.y) * cameraComponent.Zoom + screenHeight * 0.5f
                };
                glm::vec2 size = sprite.Size * cameraComponent.Zoom;

                renderer.DrawQuad({ screenCenter.x - size.x * 0.5f, screenCenter.y - size.y * 0.5f }, size, sprite.Color);
            }
        }

        renderer.EndScene();
    }

}
