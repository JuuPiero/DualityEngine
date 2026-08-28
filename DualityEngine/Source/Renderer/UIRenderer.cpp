#include "DualityEngine/Renderer/UIRenderer.h"

#include <algorithm>
#include <vector>

#include "DualityEngine/Input/Input.h"
#include "DualityEngine/Renderer/DrawHelpers2D.h"
#include "DualityEngine/Renderer/SceneRenderer.h"

namespace Duality {

    void ResolveUIRect(Scene& scene, Entity entity, glm::vec2& outTopLeft, glm::vec2& outSize) {
        auto& rect = entity.GetComponent<UIRectComponent>();

        glm::vec2 parentTopLeft{ 0.0f, 0.0f };
        glm::vec2 parentSize;
        Entity parent = entity.GetComponent<HierarchyComponent>().Parent;
        if (parent && parent.HasComponent<UIRectComponent>()) {
            ResolveUIRect(scene, parent, parentTopLeft, parentSize);
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

    static bool PointerInsideRect(Scene& scene, Entity entity, glm::vec2 pointer, Screen pointerScreen) {
        auto& rect = entity.GetComponent<UIRectComponent>();
        if (rect.Screen != pointerScreen)
            return false;
        glm::vec2 topLeft, size;
        ResolveUIRect(scene, entity, topLeft, size);
        return pointer.x >= topLeft.x && pointer.x <= topLeft.x + size.x &&
               pointer.y >= topLeft.y && pointer.y <= topLeft.y + size.y;
    }

    void UpdateUIInteractions(Scene& scene) {
        glm::vec2 pointer = Input::GetPointerPosition();
        bool pointerDown = Input::GetPointerDown();
        Screen pointerScreen = Input::GetPointerScreen();

        auto view = scene.Registry().view<UIRectComponent, UIButtonComponent>();
        for (auto handle : view) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& button = view.get<UIButtonComponent>(handle);
            if (!button.Enabled || !view.get<UIRectComponent>(handle).Enabled)
                continue;
            bool inside = PointerInsideRect(scene, Entity(handle, &scene), pointer, pointerScreen);
            if (!inside && view.get<UIRectComponent>(handle).Screen != pointerScreen) {
                button.IsHovered = false;
                button.IsPressed = false;
                button.WasClicked = false;
                continue;
            }
            bool wasPressed = button.IsPressed;
            button.IsHovered = inside;
            button.IsPressed = inside && pointerDown;
            button.WasClicked = wasPressed && !button.IsPressed && inside;
        }

        auto sliderView = scene.Registry().view<UIRectComponent, UISliderComponent>();
        for (auto handle : sliderView) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& slider = sliderView.get<UISliderComponent>(handle);
            if (!slider.Enabled || !sliderView.get<UIRectComponent>(handle).Enabled)
                continue;
            bool inside = PointerInsideRect(scene, Entity(handle, &scene), pointer, pointerScreen);
            slider.IsHovered = inside;
            if (inside && pointerDown) {
                glm::vec2 topLeft, size;
                ResolveUIRect(scene, Entity(handle, &scene), topLeft, size);
                float t = (pointer.x - topLeft.x) / std::max(size.x, 1.0f);
                t = std::clamp(t, 0.0f, 1.0f);
                slider.Value = slider.MinValue + t * (slider.MaxValue - slider.MinValue);
                slider.IsDragging = true;
            } else if (!pointerDown) {
                slider.IsDragging = false;
            }
        }

        auto toggleView = scene.Registry().view<UIRectComponent, UIToggleComponent>();
        for (auto handle : toggleView) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& toggle = toggleView.get<UIToggleComponent>(handle);
            if (!toggle.Enabled || !toggleView.get<UIRectComponent>(handle).Enabled)
                continue;
            bool inside = PointerInsideRect(scene, Entity(handle, &scene), pointer, pointerScreen);
            toggle.IsHovered = inside;
            toggle.WasToggled = false;
            if (inside && Input::GetPointerUp()) {
                toggle.IsOn = !toggle.IsOn;
                toggle.WasToggled = true;
            }
        }

        auto inputView = scene.Registry().view<UIRectComponent, UIInputFieldComponent>();
        for (auto handle : inputView) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& field = inputView.get<UIInputFieldComponent>(handle);
            if (!field.Enabled || !inputView.get<UIRectComponent>(handle).Enabled)
                continue;
            bool inside = PointerInsideRect(scene, Entity(handle, &scene), pointer, pointerScreen);
            field.IsHovered = inside;
            if (inside && Input::GetPointerUp())
                field.IsFocused = true;
            else if (Input::GetPointerUp())
                field.IsFocused = false;
        }
    }

    void RenderScreenUI(IRenderer2D& renderer, Scene& scene, Screen screen) {
        struct UIItem { entt::entity Handle; int SortOrder; };
        std::vector<UIItem> items;
        auto collectView = scene.Registry().view<UIRectComponent, UIImageComponent>();
        for (auto handle : collectView) {
            auto& rect = collectView.get<UIRectComponent>(handle);
            if (rect.Screen != screen)
                continue;
            if (!rect.Enabled || !collectView.get<UIImageComponent>(handle).Enabled)
                continue;
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            int sort = rect.SortOrder;
            Entity current(handle, &scene);
            while (current) {
                if (current.HasComponent<CanvasComponent>())
                    sort += current.GetComponent<CanvasComponent>().SortOrder * 1000;
                current = current.GetComponent<HierarchyComponent>().Parent;
            }
            items.push_back({ handle, sort });
        }
        std::sort(items.begin(), items.end(), [](const UIItem& a, const UIItem& b) { return a.SortOrder < b.SortOrder; });

        for (auto& item : items) {
            auto handle = item.Handle;
            auto& rect = scene.Registry().get<UIRectComponent>(handle);
            auto& image = scene.Registry().get<UIImageComponent>(handle);

            glm::vec2 topLeft, size;
            ResolveUIRect(scene, Entity(handle, &scene), topLeft, size);

            glm::vec4 color = image.Color;
            if (scene.Registry().all_of<UIButtonComponent>(handle)) {
                auto& button = scene.Registry().get<UIButtonComponent>(handle);
                color = button.IsPressed ? button.PressedColor : (button.IsHovered ? button.HoverColor : button.NormalColor);
            } else if (scene.Registry().all_of<UIToggleComponent>(handle)) {
                auto& toggle = scene.Registry().get<UIToggleComponent>(handle);
                if (toggle.IsOn)
                    color = glm::vec4(0.6f, 0.85f, 0.6f, 1.0f);
            } else if (scene.Registry().all_of<UISliderComponent>(handle)) {
                auto& slider = scene.Registry().get<UISliderComponent>(handle);
                float t = (slider.Value - slider.MinValue) / std::max(slider.MaxValue - slider.MinValue, 0.0001f);
                glm::vec2 fillSize{ size.x * t, size.y };
                renderer.DrawQuad(topLeft, size, { 0.2f, 0.2f, 0.2f, 1.0f });
                renderer.DrawQuad(topLeft, fillSize, { 0.4f, 0.7f, 1.0f, 1.0f });
                continue;
            } else if (scene.Registry().all_of<UIInputFieldComponent>(handle)) {
                auto& field = scene.Registry().get<UIInputFieldComponent>(handle);
                color = field.IsFocused ? glm::vec4(1.0f) : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
                renderer.DrawQuad(topLeft, size, color);
                continue;
            }

            uint32_t textureId = ResolveSpriteTexture(renderer, image.Texture);
            if (image.SliceBorder.x > 0.0f || image.SliceBorder.y > 0.0f || image.SliceBorder.z > 0.0f || image.SliceBorder.w > 0.0f)
                DrawNineSlice(renderer, topLeft, size, color, textureId, image.SliceBorder);
            else
                renderer.DrawQuad(topLeft, size, color, 0.0f, textureId);
        }
    }

}
