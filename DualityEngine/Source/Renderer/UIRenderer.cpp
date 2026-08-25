#include "DualityEngine/Renderer/UIRenderer.h"

#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Renderer/SceneRenderer.h"

namespace Duality {

    void ResolveUIRect(Scene& scene, Entity entity, glm::vec2& outTopLeft, glm::vec2& outSize) {
        auto& rect = entity.GetComponent<UIRectComponent>();

        glm::vec2 parentTopLeft{ 0.0f, 0.0f };
        glm::vec2 parentSize;
        Entity parent = entity.GetComponent<HierarchyComponent>().Parent;
        if (parent && parent.HasComponent<UIRectComponent>()) {
            ResolveUIRect(scene, parent, parentTopLeft, parentSize); // recursive -- see this function's own doc comment
        } else {
            parentSize.x = (rect.Screen == Screen::Top) ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
            parentSize.y = (rect.Screen == Screen::Top) ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);
        }

        outSize = rect.Size;
        float x, y;
        switch (rect.Anchor) {
            case UIAnchor::TopCenter:    x = parentSize.x * 0.5f + rect.Offset.x - outSize.x * 0.5f; y = rect.Offset.y; break;
            case UIAnchor::TopRight:     x = parentSize.x - rect.Offset.x - outSize.x; y = rect.Offset.y; break;
            case UIAnchor::MiddleLeft:   x = rect.Offset.x; y = parentSize.y * 0.5f + rect.Offset.y - outSize.y * 0.5f; break;
            case UIAnchor::MiddleCenter: x = parentSize.x * 0.5f + rect.Offset.x - outSize.x * 0.5f; y = parentSize.y * 0.5f + rect.Offset.y - outSize.y * 0.5f; break;
            case UIAnchor::MiddleRight:  x = parentSize.x - rect.Offset.x - outSize.x; y = parentSize.y * 0.5f + rect.Offset.y - outSize.y * 0.5f; break;
            case UIAnchor::BottomLeft:   x = rect.Offset.x; y = parentSize.y - rect.Offset.y - outSize.y; break;
            case UIAnchor::BottomCenter: x = parentSize.x * 0.5f + rect.Offset.x - outSize.x * 0.5f; y = parentSize.y - rect.Offset.y - outSize.y; break;
            case UIAnchor::BottomRight:  x = parentSize.x - rect.Offset.x - outSize.x; y = parentSize.y - rect.Offset.y - outSize.y; break;
            case UIAnchor::TopLeft:
            default:                     x = rect.Offset.x; y = rect.Offset.y; break;
        }
        outTopLeft = { parentTopLeft.x + x, parentTopLeft.y + y };
    }

    void UpdateUIInteractions(Scene& scene) {
        glm::vec2 pointer = Input::GetPointerPosition();
        bool pointerDown = Input::GetPointerDown();
        Screen pointerScreen = Input::GetPointerScreen();

        auto view = scene.Registry().view<UIRectComponent, UIButtonComponent>();
        for (auto handle : view) {
            // An inactive/hidden button shouldn't be clickable -- its IsHovered/IsPressed/
            // WasClicked simply stop updating while inactive (a script reading them while
            // it's disabled would see whatever they were last set to, same staleness a
            // disabled-but-still-queried MonoBehaviour would show in Unity).
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;

            auto& rect = view.get<UIRectComponent>(handle);
            auto& button = view.get<UIButtonComponent>(handle);

            // Top (400x240) and Bottom (320x240) screen-local pixel ranges overlap, so without
            // this a button on one screen could hover/click from a pointer actually on the
            // OTHER screen at the same local position -- Input now tags which screen the
            // pointer is actually over (see GetPointerScreen's own comment), so this is a real
            // fix, not just a defensive check.
            if (rect.Screen != pointerScreen) {
                button.IsHovered = false;
                button.IsPressed = false;
                button.WasClicked = false;
                continue;
            }

            glm::vec2 topLeft, size;
            ResolveUIRect(scene, Entity(handle, &scene), topLeft, size);
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
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& image = view.get<UIImageComponent>(handle);

            glm::vec2 topLeft, size;
            ResolveUIRect(scene, Entity(handle, &scene), topLeft, size);

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
