// The UI widgets / effects the UI renderer delegates here: Scrollbar handle, Dropdown caption
// and open list, Shadow / Outline, and text measuring for layout (Content Size Fitter).
#include "DualityEngine/Renderer/UIWidgetRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        IRenderer2D* s_UILayoutRenderer = nullptr;

        uint32_t ResolveFontId(IRenderer2D& renderer, const AssetRef& font) {
            if (font.Guid.empty())
                return 0;
            const std::string path = AssetDatabase::ResolvePath(font.Guid);
            return path.empty() ? 0 : renderer.LoadFont(path);
        }


        std::string DropdownOption(const std::string& options, int index) {
            size_t start = 0;
            for (int i = 0; i < index; ++i) {
                const size_t bar = options.find('|', start);
                if (bar == std::string::npos)
                    return {};
                start = bar + 1;
            }
            const size_t end = options.find('|', start);
            return options.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }

        int DropdownOptionCount(const std::string& options) {
            return 1 + static_cast<int>(std::count(options.begin(), options.end(), '|'));
        }

        glm::vec4 WithAlpha(glm::vec4 color, float alpha) {
            color.a *= alpha;
            return color;
        }
    }

    void SetUILayoutRenderer(IRenderer2D* renderer) {
        s_UILayoutRenderer = renderer;
    }

    glm::vec2 MeasureUITextForLayout(const UITextComponent& text) {
        if (s_UILayoutRenderer) {
            const uint32_t fontId = ResolveFontId(*s_UILayoutRenderer, text.Font);
            const glm::vec2 measured = s_UILayoutRenderer->MeasureText(text.Text, text.FontSize, fontId);
            return { measured.x, std::max(measured.y, text.FontSize) };
        }
        return { text.FontSize * 0.55f * static_cast<float>(text.Text.size()), text.FontSize };
    }

    void DrawUIScrollbar(IRenderer2D& renderer, Scene& scene, entt::entity handle, const glm::vec2& topLeft,
                         const glm::vec2& size, const glm::vec4& trackColor, float alpha) {
        const auto& bar = scene.Registry().get<UIScrollbarComponent>(handle);
        renderer.DrawQuad(topLeft, size, WithAlpha(trackColor, alpha));
        const int axis = bar.Direction == ScrollbarDirection::LeftToRight || bar.Direction == ScrollbarDirection::RightToLeft ? 0 : 1;
        const bool reversed = bar.Direction == ScrollbarDirection::RightToLeft || bar.Direction == ScrollbarDirection::BottomToTop;
        const float trackLength = size[axis];
        const float handleLength = trackLength * std::clamp(bar.Size, 0.05f, 1.0f);
        float t = std::clamp(bar.Value, 0.0f, 1.0f);
        if (reversed)
            t = 1.0f - t;
        glm::vec2 handlePos = topLeft, handleSize = size;
        handlePos[axis] += (trackLength - handleLength) * t;
        handleSize[axis] = handleLength;
        const float inset = std::min(2.0f, size[1 - axis] * 0.2f);
        handlePos[1 - axis] += inset;
        handleSize[1 - axis] -= inset * 2.0f;
        glm::vec4 color = bar.HandleColor;
        if (bar.IsDragging)
            color = glm::vec4(glm::vec3(color) * 0.8f, color.a);
        renderer.DrawQuad(handlePos, handleSize, WithAlpha(color, alpha));
    }

    void DrawUIDropdownCaption(IRenderer2D& renderer, Scene& scene, entt::entity handle, const glm::vec2& topLeft,
                               const glm::vec2& size, const glm::vec4& boxColor, float alpha) {
        const auto& dropdown = scene.Registry().get<UIDropdownComponent>(handle);
        renderer.DrawQuad(topLeft, size, WithAlpha(boxColor, alpha));
        const uint32_t fontId = ResolveFontId(renderer, dropdown.Font);
        const float textY = topLeft.y + (size.y - dropdown.FontSize) * 0.5f;
        renderer.DrawText(DropdownOption(dropdown.Options, dropdown.Value), { topLeft.x + 6.0f, textY }, dropdown.FontSize,
                          WithAlpha(dropdown.TextColor, alpha), fontId);
        // Arrow: a small "v" made of two quads (no glyph dependency, identical on 3DS).
        const float arrow = std::min(size.y * 0.3f, 8.0f);
        const glm::vec2 center{ topLeft.x + size.x - arrow - 6.0f, topLeft.y + size.y * 0.5f };
        const glm::vec4 arrowColor = WithAlpha(dropdown.TextColor, alpha);
        const float thickness = std::max(1.5f, arrow * 0.25f);
        const float half = arrow * 0.75f;
        renderer.DrawQuad({ center.x - half * 0.5f - half * 0.5f, center.y - thickness * 0.5f }, { half * 1.2f, thickness }, arrowColor, 45.0f);
        renderer.DrawQuad({ center.x + half * 0.5f - half * 0.7f, center.y - thickness * 0.5f }, { half * 1.2f, thickness }, arrowColor, -45.0f);
    }

    void DrawUIDropdownLists(IRenderer2D& renderer, Scene& scene, Screen screen) {
        auto& registry = scene.Registry();
        for (auto handle : registry.view<UIDropdownComponent, UIRectComponent>()) {
            const auto& dropdown = registry.get<UIDropdownComponent>(handle);
            Entity entity(handle, &scene);
            if (!dropdown.IsExpanded || !dropdown.Enabled || !IsUIElementOnCanvas(scene, entity, screen) || !scene.IsEffectivelyActive(entity))
                continue;
            glm::vec2 topLeft, size;
            ResolveUIRect(scene, entity, topLeft, size);
            const float alpha = ResolveCanvasGroupAlpha(scene, entity);
            const int count = DropdownOptionCount(dropdown.Options);
            const float itemHeight = std::max(dropdown.ItemHeight, 4.0f);
            const float screenHeight = static_cast<float>(screen == Screen::Top ? TopScreenHeight : BottomScreenHeight);
            float listTop = topLeft.y + size.y;
            if (listTop + itemHeight * count > screenHeight)
                listTop = topLeft.y - itemHeight * count;
            const uint32_t fontId = ResolveFontId(renderer, dropdown.Font);
            renderer.DrawQuad({ topLeft.x - 1.0f, listTop - 1.0f }, { size.x + 2.0f, itemHeight * count + 2.0f }, WithAlpha({ 0.2f, 0.2f, 0.2f, 1.0f }, alpha));
            for (int i = 0; i < count; ++i) {
                const glm::vec2 itemPos{ topLeft.x, listTop + itemHeight * i };
                glm::vec4 color = dropdown.ItemColor;
                if (i == dropdown.HoveredItem)
                    color = dropdown.HighlightColor;
                else if (i == dropdown.Value)
                    color = glm::mix(dropdown.ItemColor, dropdown.HighlightColor, 0.45f);
                renderer.DrawQuad(itemPos, { size.x, itemHeight }, WithAlpha(color, alpha));
                renderer.DrawText(DropdownOption(dropdown.Options, i), { itemPos.x + 6.0f, itemPos.y + (itemHeight - dropdown.FontSize) * 0.5f },
                                  dropdown.FontSize, WithAlpha(dropdown.TextColor, alpha), fontId);
            }
        }
    }

}
