#pragma once

#include <entt.hpp>

namespace Duality {

    class Scene;

    // Thin handle around an entt entity + owning Scene, mirroring the pattern
    // already proven out in the MyGameEngine prototype.
    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene) : m_Handle(handle), m_Scene(scene) {}

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args);

        template<typename T>
        T& GetComponent();
        template<typename T>
        const T& GetComponent() const;

        template<typename T>
        bool HasComponent() const;

        template<typename T>
        void RemoveComponent();

        entt::entity Handle() const { return m_Handle; }
        Scene* GetScene() const { return m_Scene; }

        operator bool() const { return m_Handle != entt::null; }
        bool operator==(const Entity& other) const { return m_Handle == other.m_Handle && m_Scene == other.m_Scene; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_Handle{ entt::null };
        Scene* m_Scene = nullptr;
    };

}
