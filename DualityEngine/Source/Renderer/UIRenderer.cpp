#include "DualityEngine/Renderer/UIRenderer.h"

#include <algorithm>
#include <vector>

#include "DualityEngine/Asset/AssetDatabase.h"
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

        // RectTransform-style resolution, per axis independently. AnchorMin==AnchorMax on an
        // axis is a point anchor (Pivot places this rect's own point at the anchor point, offset
        // by AnchoredPosition); otherwise the axis stretches to fill the anchor span, and Pivot
        // has no effect on that axis (matches Unity exactly).
        for (int axis = 0; axis < 2; axis++) {
            float minPx = parentTopLeft[axis] + rect.AnchorMin[axis] * parentSize[axis];
            float maxPx = parentTopLeft[axis] + rect.AnchorMax[axis] * parentSize[axis];
            if (rect.AnchorMin[axis] == rect.AnchorMax[axis]) {
                outSize[axis] = rect.SizeDelta[axis];
                outTopLeft[axis] = minPx + rect.AnchoredPosition[axis] - rect.Pivot[axis] * outSize[axis];
            } else {
                outSize[axis] = (maxPx - minPx) + rect.SizeDelta[axis];
                outTopLeft[axis] = (minPx + maxPx) * 0.5f + rect.AnchoredPosition[axis] - outSize[axis] * 0.5f;
            }
        }
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

    // Resolves fontRef to a loaded font handle via AssetDatabase + IRenderer2D::LoadFont, or 0
    // (default/system font) if the ref is empty or doesn't resolve to an existing file -- same
    // guid->path->Load chain/graceful-degradation convention as SceneRenderer.cpp's
    // ResolveSpriteTexture, just for fonts and only ever needed here so far.
    static uint32_t ResolveFont(IRenderer2D& renderer, const AssetRef& fontRef) {
        if (fontRef.Guid.empty())
            return 0;
        std::string path = AssetDatabase::ResolvePath(fontRef.Guid);
        if (path.empty())
            return 0;
        return renderer.LoadFont(path);
    }

    void RenderScreenUI(IRenderer2D& renderer, Scene& scene, Screen screen) {
        // Kind-tagged, not just UIImageComponent-shaped: a UIRectComponent+UITextComponent
        // entity (no Image at all -- exactly what UIDocument's <Text> tag produces) has to
        // share this same sorted draw list so Text and Image widgets interleave correctly under
        // a shared CanvasComponent, but the per-item draw step below can no longer assume every
        // item carries a UIImageComponent.
        enum class UIItemKind { Image, Text };
        struct UIItem { entt::entity Handle; int SortOrder; UIItemKind Kind; };
        std::vector<UIItem> items;

        auto resolveSort = [&](entt::entity handle, int baseSort) {
            int sort = baseSort;
            Entity current(handle, &scene);
            while (current) {
                if (current.HasComponent<CanvasComponent>())
                    sort += current.GetComponent<CanvasComponent>().SortOrder * 1000;
                current = current.GetComponent<HierarchyComponent>().Parent;
            }
            return sort;
        };

        auto imageView = scene.Registry().view<UIRectComponent, UIImageComponent>();
        for (auto handle : imageView) {
            auto& rect = imageView.get<UIRectComponent>(handle);
            if (rect.Screen != screen || !rect.Enabled || !imageView.get<UIImageComponent>(handle).Enabled)
                continue;
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            items.push_back({ handle, resolveSort(handle, rect.SortOrder), UIItemKind::Image });
        }

        auto textView = scene.Registry().view<UIRectComponent, UITextComponent>();
        for (auto handle : textView) {
            auto& rect = textView.get<UIRectComponent>(handle);
            if (rect.Screen != screen || !rect.Enabled || !textView.get<UITextComponent>(handle).Enabled)
                continue;
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            items.push_back({ handle, resolveSort(handle, rect.SortOrder), UIItemKind::Text });
        }

        std::sort(items.begin(), items.end(), [](const UIItem& a, const UIItem& b) { return a.SortOrder < b.SortOrder; });

        for (auto& item : items) {
            auto handle = item.Handle;

            glm::vec2 topLeft, size;
            ResolveUIRect(scene, Entity(handle, &scene), topLeft, size);

            if (item.Kind == UIItemKind::Text) {
                auto& text = scene.Registry().get<UITextComponent>(handle);
                uint32_t fontId = ResolveFont(renderer, text.Font);
                glm::vec2 textSize = renderer.MeasureText(text.Text, text.FontSize, fontId);
                float offsetX = 0.0f;
                if (text.Alignment == TextAlignment::Center)
                    offsetX = (size.x - textSize.x) * 0.5f;
                else if (text.Alignment == TextAlignment::Right)
                    offsetX = size.x - textSize.x;
                float offsetY = (size.y - text.FontSize) * 0.5f; // vertical always centered, see UITextComponent's own comment
                renderer.DrawText(text.Text, topLeft + glm::vec2(offsetX, offsetY), text.FontSize, text.Color, fontId);
                continue;
            }

            auto& image = scene.Registry().get<UIImageComponent>(handle);

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
