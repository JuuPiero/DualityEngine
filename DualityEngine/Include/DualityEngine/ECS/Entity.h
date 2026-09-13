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

        // Unlike operator bool(), this also verifies that the owning Scene still
        // owns a live registry entry. Keep handles returned from gameplay code
        // across frames only when this is checked before use: DestroyEntity and
        // scene reloads invalidate the old handle.
        bool IsValid() const;

        // Safe alternative to GetComponent<T>(). A missing component or a stale
        // Entity returns nullptr rather than asking EnTT to assert/terminate.
        template<typename T>
        T* TryGetComponent();
        template<typename T>
        const T* TryGetComponent() const;

        template<typename T>
        void RemoveComponent();

        entt::entity Handle() const { return m_Handle; }
        Scene* GetScene() const { return m_Scene; }

        // Preserve the familiar `if (entity)` form, but make it a real liveness
        // check rather than merely testing whether the numeric handle is non-null.
        operator bool() const { return IsValid(); }
        bool operator==(const Entity& other) const { return m_Handle == other.m_Handle && m_Scene == other.m_Scene; }
        bool operator!=(const Entity& other) const { return !(*this == other); }

    private:
        entt::entity m_Handle{ entt::null };
        Scene* m_Scene = nullptr;
    };

}
