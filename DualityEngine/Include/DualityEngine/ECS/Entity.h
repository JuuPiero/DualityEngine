#pragma once

#include <vector>

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

        // Hierarchy convenience APIs, equivalent in intent to Unity's GameObject helpers.
        // Component pointers are borrowed ECS storage: use them immediately and reacquire after
        // structural changes (DestroyEntity, scene reload, component add/remove).
        Entity GetParent() const;
        std::vector<Entity> GetChildren() const;
        void ClearChildren(); // destroys every direct child and its whole descendant subtree

        template<typename T>
        T* GetComponentInChildren(bool includeSelf = true);
        template<typename T>
        const T* GetComponentInChildren(bool includeSelf = true) const;
        template<typename T>
        std::vector<T*> GetComponentsInChildren(bool includeSelf = true);
        template<typename T>
        std::vector<const T*> GetComponentsInChildren(bool includeSelf = true) const;

        template<typename T>
        T* GetComponentInParent(bool includeSelf = true);
        template<typename T>
        const T* GetComponentInParent(bool includeSelf = true) const;
        template<typename T>
        std::vector<T*> GetComponentsInParent(bool includeSelf = true);
        template<typename T>
        std::vector<const T*> GetComponentsInParent(bool includeSelf = true) const;

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
