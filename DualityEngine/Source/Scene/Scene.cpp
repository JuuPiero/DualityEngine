#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    Entity Scene::CreateEntity(const std::string& name) {
        Entity entity(m_Registry.create(), this);
        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    void Scene::DestroyEntity(Entity entity) {
        m_Registry.destroy(entity.Handle());
    }

    Entity Scene::GetPrimaryCamera(Screen screen) {
        auto view = m_Registry.view<CameraComponent>();
        for (auto handle : view) {
            const auto& camera = view.get<CameraComponent>(handle);
            if (camera.Screen == screen && camera.Primary)
                return Entity(handle, this);
        }
        return Entity{};
    }

    void Scene::OnRuntimeStart() {
        auto view = m_Registry.view<BehaviourComponent>();
        for (auto handle : view) {
            auto& bc = view.get<BehaviourComponent>(handle);
            if (bc.Instance || bc.ClassName.empty())
                continue;

            if (ScriptRegistry::TryCreate(bc.ClassName, &bc.Instance, &bc.Destroy)) {
                bc.Instance->m_Entity = Entity(handle, this);
                bc.Instance->OnCreate();
            } else {
                Log::Error("Behaviour: unknown script class '" + bc.ClassName + "'");
            }
        }
    }

    void Scene::OnRuntimeUpdate(float deltaTime) {
        auto view = m_Registry.view<BehaviourComponent>();
        for (auto handle : view) {
            auto& bc = view.get<BehaviourComponent>(handle);
            if (bc.Instance)
                bc.Instance->OnUpdate(deltaTime);
        }
    }

    void Scene::OnRuntimeStop() {
        auto view = m_Registry.view<BehaviourComponent>();
        for (auto handle : view) {
            auto& bc = view.get<BehaviourComponent>(handle);
            if (bc.Instance) {
                bc.Instance->OnDestroy();
                bc.Destroy(bc.Instance);
                bc.Instance = nullptr;
            }
        }
    }

}
