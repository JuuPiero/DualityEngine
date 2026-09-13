#pragma once

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    // Script-facing fixed-budget 2D polyline. It is intentionally a component wrapper rather
    // than a Behaviour convenience accessor: use LineRenderer(GetEntity()) explicitly, just
    // like SpriteRenderer and Rigidbody2D. Eight points cap GPU work on Nintendo 3DS.
    class LineRenderer {
    public:
        static constexpr int MaxPositions = 8;

        explicit LineRenderer(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<LineRendererComponent>(); }

        bool GetEnabled() const { return *this && m_Entity.GetComponent<LineRendererComponent>().Enabled; }
        void SetEnabled(bool enabled) { if (*this) m_Entity.GetComponent<LineRendererComponent>().Enabled = enabled; }

        glm::vec4 GetColor() const { return *this ? m_Entity.GetComponent<LineRendererComponent>().Color : glm::vec4{ 1.0f }; }
        void SetColor(const glm::vec4& color) { if (*this) m_Entity.GetComponent<LineRendererComponent>().Color = color; }

        float GetWidth() const { return *this ? m_Entity.GetComponent<LineRendererComponent>().Width : 0.0f; }
        void SetWidth(float width) {
            if (*this)
                m_Entity.GetComponent<LineRendererComponent>().Width = std::max(width, 0.0f);
        }

        bool GetLoop() const { return *this && m_Entity.GetComponent<LineRendererComponent>().Loop; }
        void SetLoop(bool loop) { if (*this) m_Entity.GetComponent<LineRendererComponent>().Loop = loop; }

        bool GetUseWorldSpace() const { return *this && m_Entity.GetComponent<LineRendererComponent>().UseWorldSpace; }
        void SetUseWorldSpace(bool useWorldSpace) { if (*this) m_Entity.GetComponent<LineRendererComponent>().UseWorldSpace = useWorldSpace; }

        int GetPositionCount() const {
            return *this ? std::clamp(m_Entity.GetComponent<LineRendererComponent>().PointCount, 0, MaxPositions) : 0;
        }
        void SetPositionCount(int count) {
            if (*this)
                m_Entity.GetComponent<LineRendererComponent>().PointCount = std::clamp(count, 0, MaxPositions);
        }

        glm::vec3 GetPosition(int index) const {
            if (!*this || index < 0 || index >= GetPositionCount())
                return {};
            return GetLinePoint(m_Entity.GetComponent<LineRendererComponent>(), index);
        }
        bool SetPosition(int index, const glm::vec3& position) {
            if (!*this || index < 0 || index >= MaxPositions)
                return false;
            LineRendererComponent& line = m_Entity.GetComponent<LineRendererComponent>();
            GetLinePoint(line, index) = position;
            if (line.PointCount <= index)
                line.PointCount = index + 1;
            return true;
        }

        // Copies up to MaxPositions. Returns false only when this Entity has no Line Renderer.
        bool SetPositions(const std::vector<glm::vec3>& positions) {
            if (!*this)
                return false;
            LineRendererComponent& line = m_Entity.GetComponent<LineRendererComponent>();
            line.PointCount = std::min(static_cast<int>(positions.size()), MaxPositions);
            for (int i = 0; i < line.PointCount; ++i)
                GetLinePoint(line, i) = positions[static_cast<size_t>(i)];
            return true;
        }

    private:
        Entity m_Entity;
    };

}
