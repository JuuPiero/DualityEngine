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

        UIAnchor GetAnchor() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().Anchor : UIAnchor::TopLeft;
        }
        void SetAnchor(UIAnchor anchor) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().Anchor = anchor;
        }

        glm::vec2 GetOffset() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().Offset : glm::vec2{};
        }
        void SetOffset(const glm::vec2& offset) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().Offset = offset;
        }

        glm::vec2 GetSize() const {
            return m_Entity ? m_Entity.GetComponent<UIRectComponent>().Size : glm::vec2{};
        }
        void SetSize(const glm::vec2& size) {
            if (*this)
                m_Entity.GetComponent<UIRectComponent>().Size = size;
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

    // UITextComponent -- string content (rendering not implemented yet).
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
