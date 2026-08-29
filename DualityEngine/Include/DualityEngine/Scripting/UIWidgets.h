#pragma once

#include <glm/glm.hpp>
#include <string>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Renderer/UIAnchor.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // UIButtonComponent -- hover/press/click state and button colors.
    class UIButton {
    public:
        explicit UIButton(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<UIButtonComponent>(); }

        bool IsHovered() const {
            return m_Entity ? m_Entity.GetComponent<UIButtonComponent>().IsHovered : false;
        }
        bool IsPressed() const {
            return m_Entity ? m_Entity.GetComponent<UIButtonComponent>().IsPressed : false;
        }
        bool WasClicked() const {
            return m_Entity ? m_Entity.GetComponent<UIButtonComponent>().WasClicked : false;
        }

        glm::vec4 GetNormalColor() const {
            return m_Entity ? m_Entity.GetComponent<UIButtonComponent>().NormalColor : glm::vec4{ 1.0f };
        }
        void SetNormalColor(const glm::vec4& color) {
            if (*this)
                m_Entity.GetComponent<UIButtonComponent>().NormalColor = color;
        }
        glm::vec4 GetHoverColor() const {
            return m_Entity ? m_Entity.GetComponent<UIButtonComponent>().HoverColor : glm::vec4{ 1.0f };
        }
        void SetHoverColor(const glm::vec4& color) {
            if (*this)
                m_Entity.GetComponent<UIButtonComponent>().HoverColor = color;
        }
        glm::vec4 GetPressedColor() const {
            return m_Entity ? m_Entity.GetComponent<UIButtonComponent>().PressedColor : glm::vec4{ 1.0f };
        }
        void SetPressedColor(const glm::vec4& color) {
            if (*this)
                m_Entity.GetComponent<UIButtonComponent>().PressedColor = color;
        }

    private:
        Entity m_Entity;
    };

    // UIRectComponent -- screen-space layout in fixed pixels.
    class UIRect {
    public:
        explicit UIRect(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<UIRectComponent>(); }

        Screen GetScreen() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().Screen : Screen::Top;
        }
        void SetScreen(Screen screen) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().Screen = screen;
        }

        glm::vec2 GetAnchorMin() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().AnchorMin : glm::vec2{};
        }
        void SetAnchorMin(const glm::vec2& anchorMin) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().AnchorMin = anchorMin;
        }

        glm::vec2 GetAnchorMax() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().AnchorMax : glm::vec2{};
        }
        void SetAnchorMax(const glm::vec2& anchorMax) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().AnchorMax = anchorMax;
        }

        glm::vec2 GetPivot() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().Pivot : glm::vec2{};
        }
        void SetPivot(const glm::vec2& pivot) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().Pivot = pivot;
        }

        glm::vec2 GetAnchoredPosition() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().AnchoredPosition : glm::vec2{};
        }
        void SetAnchoredPosition(const glm::vec2& anchoredPosition) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().AnchoredPosition = anchoredPosition;
        }

        glm::vec2 GetSizeDelta() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().SizeDelta : glm::vec2{};
        }
        void SetSizeDelta(const glm::vec2& sizeDelta) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().SizeDelta = sizeDelta;
        }

        // One-call convenience matching the Editor's own "Anchor Presets" quick-set buttons
        // (PropertiesPanel.cpp) -- snaps AnchorMin/AnchorMax/Pivot to a point anchor, leaving
        // AnchoredPosition/SizeDelta untouched. Scripts that only need Unity's classic 9-way
        // anchor grid (no stretch) can use this instead of setting all three vectors by hand.
        void SetAnchorPreset(UIAnchor preset) {
            if (!*this)
                return;
            auto& rect = m_Entity.GetComponent<UIRectComponent>();
            UIAnchorPresetToMinMaxPivot(preset, rect.AnchorMin, rect.AnchorMax, rect.Pivot);
        }

    private:
        Entity m_Entity;
    };

    // UIImageComponent helpers.
    class UIImage {
    public:
        explicit UIImage(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<UIImageComponent>(); }

        glm::vec4 GetColor() const {
            return m_Entity ? m_Entity.GetComponent<UIImageComponent>().Color : glm::vec4{ 1.0f };
        }
        void SetColor(const glm::vec4& color) {
            if (*this)
                m_Entity.GetComponent<UIImageComponent>().Color = color;
        }

        AssetRef GetTexture() const {
            return m_Entity ? m_Entity.GetComponent<UIImageComponent>().Texture : AssetRef{};
        }
        void SetTexture(const AssetRef& texture) {
            if (*this)
                m_Entity.GetComponent<UIImageComponent>().Texture = texture;
        }

    private:
        Entity m_Entity;
    };

    // UITextComponent -- string content, drawn by UIRenderer.cpp via IRenderer2D::DrawText.
    class UIText {
    public:
        explicit UIText(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<UITextComponent>(); }

        std::string GetText() const {
            return m_Entity ? m_Entity.GetComponent<UITextComponent>().Text : std::string{};
        }
        void SetText(const std::string& text) {
            if (*this)
                m_Entity.GetComponent<UITextComponent>().Text = text;
        }

    private:
        Entity m_Entity;
    };

}
