#pragma once

#include <string>

#include <entt.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    class Scene {
    public:
        Scene() = default;

        Entity CreateEntity(const std::string& name = "Entity");
        void DestroyEntity(Entity entity);

        // Returns the first entity found with a CameraComponent targeting
        // `screen` and Primary == true, or an empty Entity if none exists.
        Entity GetPrimaryCamera(Screen screen);

        // Runtime (Play mode) lifecycle -- instantiates/updates/destroys
        // every entity's BehaviourComponent, if any.
        void OnRuntimeStart();
        void OnRuntimeUpdate(float deltaTime);
        void OnRuntimeStop();

        entt::registry& Registry() { return m_Registry; }

    private:
        entt::registry m_Registry;

        // Opaque (actually b2World*) so this header doesn't need to include
        // Box2D -- only non-null between OnRuntimeStart and OnRuntimeStop.
        void* m_PhysicsWorld = nullptr;
    };

    // --- Entity template method definitions -------------------------------
    // Defined here (after Scene is complete) rather than in Entity.h, since
    // they need Scene::Registry() to be a complete type.

    template<typename T, typename... Args>
    T& Entity::AddComponent(Args&&... args) {
        return m_Scene->Registry().emplace<T>(m_Handle, std::forward<Args>(args)...);
    }

    template<typename T>
    T& Entity::GetComponent() {
        return m_Scene->Registry().get<T>(m_Handle);
    }

    template<typename T>
    bool Entity::HasComponent() const {
        return m_Scene->Registry().all_of<T>(m_Handle);
    }

    template<typename T>
    void Entity::RemoveComponent() {
        m_Scene->Registry().remove<T>(m_Handle);
    }

}
