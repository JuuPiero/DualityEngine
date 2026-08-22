#include "DualityEngine/Renderer/SceneRenderer.h"

#include "DualityEngine/Scene/Components.h"

namespace Duality {

    void RenderScreen(IRenderer2D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor) {
        renderer.BeginScene(screen, clearColor);

        Entity camera = scene.GetPrimaryCamera(screen);
        if (camera && camera.HasComponent<SpriteRendererComponent>()) {
            auto& transform = camera.GetComponent<TransformComponent>();
            auto& sprite = camera.GetComponent<SpriteRendererComponent>();
            renderer.DrawQuad(
                { transform.Translation.x - sprite.Size.x * 0.5f, transform.Translation.y - sprite.Size.y * 0.5f },
                sprite.Size, sprite.Color);
        }

        renderer.EndScene();
    }

}
