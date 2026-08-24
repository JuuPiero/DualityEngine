#include "DualityEngine/Renderer/UIRenderer.h"

#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Renderer/SceneRenderer.h"

namespace Duality {

    void ResolveUIRect(const UIRectComponent& rect, glm::vec2& outTopLeft, glm::vec2& outSize) {
        float screenWidth = (rect.Screen == Screen::Top) ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
        float screenHeight = (rect.Screen == Screen::Top) ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);
        outSize = rect.Size;

        float x, y;
        switch (rect.Anchor) {
            case UIAnchor::TopCenter:    x = screenWidth * 0.5f + rect.Offset.x - outSize.x * 0.5f; y = rect.Offset.y; break;
            case UIAnchor::TopRight:     x = screenWidth - rect.Offset.x - outSize.x; y = rect.Offset.y; break;
            case UIAnchor::MiddleLeft:   x = rect.Offset.x; y = screenHeight * 0.5f + rect.Offset.y - outSize.y * 0.5f; break;
            case UIAnchor::MiddleCenter: x = screenWidth * 0.5f + rect.Offset.x - outSize.x * 0.5f; y = screenHeight * 0.5f + rect.Offset.y - outSize.y * 0.5f; break;
            case UIAnchor::MiddleRight:  x = screenWidth - rect.Offset.x - outSize.x; y = screenHeight * 0.5f + rect.Offset.y - outSize.y * 0.5f; break;
            case UIAnchor::BottomLeft:   x = rect.Offset.x; y = screenHeight - rect.Offset.y - outSize.y; break;
            case UIAnchor::BottomCenter: x = screenWidth * 0.5f + rect.Offset.x - outSize.x * 0.5f; y = screenHeight - rect.Offset.y - outSize.y; break;
            case UIAnchor::BottomRight:  x = screenWidth - rect.Offset.x - outSize.x; y = screenHeight - rect.Offset.y - outSize.y; break;
            case UIAnchor::TopLeft:
            default:                     x = rect.Offset.x; y = rect.Offset.y; break;
        }
        outTopLeft = { x, y };
    }

    void UpdateUIInteractions(Scene& scene) {
        glm::vec2 pointer = Input::GetPointerPosition();
        bool pointerDown = Input::GetPointerDown();

        auto view = scene.Registry().view<UIRectComponent, UIButtonComponent>();
        for (auto handle : view) {
            auto& rect = view.get<UIRectComponent>(handle);
            auto& button = view.get<UIButtonComponent>(handle);

            glm::vec2 topLeft, size;
            ResolveUIRect(rect, topLeft, size);
            bool inside = pointer.x >= topLeft.x && pointer.x <= topLeft.x + size.x &&
                          pointer.y >= topLeft.y && pointer.y <= topLeft.y + size.y;

            bool wasPressed = button.IsPressed;
            button.IsHovered = inside;
            button.IsPressed = inside && pointerDown;
            // A click is a press that started (and is still, this frame) over this SAME
            // button, now released while still hovering it -- wasPressed already implies "was
            // inside on the previous frame", since IsPressed only ever gets set true while
            // inside (see above), so this can't false-positive from a press-elsewhere-drag-in.
            button.WasClicked = wasPressed && !button.IsPressed && inside;
        }
    }

    void RenderScreenUI(IRenderer2D& renderer, Scene& scene, Screen screen) {
        auto view = scene.Registry().view<UIRectComponent, UIImageComponent>();
        for (auto handle : view) {
            auto& rect = view.get<UIRectComponent>(handle);
            if (rect.Screen != screen)
                continue;
            auto& image = view.get<UIImageComponent>(handle);

            glm::vec2 topLeft, size;
            ResolveUIRect(rect, topLeft, size);

            // A UIButtonComponent's own current interaction color takes over from
            // UIImageComponent::Color -- see UIButtonComponent's own comment.
            glm::vec4 color = image.Color;
            if (scene.Registry().all_of<UIButtonComponent>(handle)) {
                auto& button = scene.Registry().get<UIButtonComponent>(handle);
                color = button.IsPressed ? button.PressedColor : (button.IsHovered ? button.HoverColor : button.NormalColor);
            }

            uint32_t textureId = ResolveSpriteTexture(renderer, image.Texture); // same guid->path->LoadTexture chain sprites use
            renderer.DrawQuad(topLeft, size, color, 0.0f, textureId); // axis-aligned only -- UIRectComponent has no rotation field in this first pass
        }
    }

}
