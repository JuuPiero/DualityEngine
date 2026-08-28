#pragma once

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/ProjectionType.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Layer.h"

namespace Duality {

    // Unity's Camera -- CameraComponent helpers.
    class Camera {
    public:
        explicit Camera(Entity entity) : m_Entity(entity) {}

        operator bool() const { return m_Entity && m_Entity.HasComponent<CameraComponent>(); }

        Screen GetScreen() const {
            return m_Entity ? m_Entity.GetComponent<CameraComponent>().Screen : Screen::Top;
        }
        void SetScreen(Screen screen) {
            if (*this)
                m_Entity.GetComponent<CameraComponent>().Screen = screen;
        }

        bool IsPrimary() const {
            return m_Entity ? m_Entity.GetComponent<CameraComponent>().Primary : false;
        }
        void SetPrimary(bool primary) {
            if (*this)
                m_Entity.GetComponent<CameraComponent>().Primary = primary;
        }

        float GetZoom() const {
            return m_Entity ? m_Entity.GetComponent<CameraComponent>().Zoom : 1.0f;
        }
        void SetZoom(float zoom) {
            if (*this)
                m_Entity.GetComponent<CameraComponent>().Zoom = zoom;
        }

        ProjectionType GetProjection() const {
            return m_Entity ? m_Entity.GetComponent<CameraComponent>().Projection : ProjectionType::Orthographic;
        }
        void SetProjection(ProjectionType projection) {
            if (*this)
                m_Entity.GetComponent<CameraComponent>().Projection = projection;
        }

        float GetFovDegrees() const {
            return m_Entity ? m_Entity.GetComponent<CameraComponent>().FovDegrees : 60.0f;
        }
        void SetFovDegrees(float fov) {
            if (*this)
                m_Entity.GetComponent<CameraComponent>().FovDegrees = fov;
        }

        glm::vec4 GetBackground() const {
            return m_Entity ? m_Entity.GetComponent<CameraComponent>().Background : glm::vec4{ 0.08f, 0.08f, 0.12f, 1.0f };
        }
        void SetBackground(const glm::vec4& color) {
            if (*this)
                m_Entity.GetComponent<CameraComponent>().Background = color;
        }

        uint32_t GetCullingMask() const {
            return m_Entity ? m_Entity.GetComponent<CameraComponent>().CullingMask : AllLayersMask;
        }
        void SetCullingMask(uint32_t mask) {
            if (*this)
                m_Entity.GetComponent<CameraComponent>().CullingMask = mask;
        }

    private:
        Entity m_Entity;
    };

}
