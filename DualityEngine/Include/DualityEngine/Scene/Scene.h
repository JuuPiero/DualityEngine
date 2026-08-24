#pragma once

#include <string>
#include <vector>

#include <entt.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    // Defined in Components.h -- forward-declared here (rather than including
    // Components.h, which includes Behaviour.h, which includes this header) so
    // GetWorldTransform can return it by value. Every call site already includes
    // Components.h for other reasons before actually using the returned value.
    struct TransformComponent;

    class Scene {
    public:
        Scene() = default;

        Entity CreateEntity(const std::string& name = "Entity");
        void DestroyEntity(Entity entity);

        // Returns the first entity found with a CameraComponent targeting
        // `screen` and Primary == true, or an empty Entity if none exists.
        Entity GetPrimaryCamera(Screen screen);

        // Root-level entities (HierarchyComponent::Parent == null), in display/
        // serialization order -- what HierarchyPanel iterates at the top of its tree.
        const std::vector<Entity>& GetRootEntities() const { return m_RootEntities; }

        // Reparents `child` under `newParent` (Entity{} = root), inserted immediately
        // after `insertAfter` in the sibling list (appended at the end if insertAfter is
        // null/not found). The same call handles a normal reparent (insertAfter={}), a
        // sibling reorder (newParent == child's current parent), and a root-list reorder
        // (newParent={}). No-ops if child == newParent or newParent is a descendant of
        // child (would create a cycle).
        //
        // preserveWorldPosition recomputes child's local TransformComponent so it doesn't
        // visually jump when reparented (Unity's default "keep world position" behavior).
        // Pass false only from SceneSerializer::Deserialize, where the just-loaded
        // Transform is already the correct local value and must not be re-derived.
        void SetParent(Entity child, Entity newParent, Entity insertAfter = {}, bool preserveWorldPosition = true);

        // Walks the Parent chain, composing local TransformComponents into one
        // world-space TransformComponent (2D: translate + Z-rotation + non-uniform XY
        // scale -- X/Y rotation is left untouched, matching every other 2D-only Transform
        // consumer in this codebase). A root entity's world transform is identical to its
        // local one, so this is a safe drop-in replacement for any direct
        // transform.Translation/.Rotation.z/.Scale read.
        TransformComponent GetWorldTransform(Entity entity);

        // Searches every ScreenGroupComponent{screen} entity's subtree (any depth, via
        // HierarchyComponent::Children) for one named `name`. Falls back to a scene-wide
        // by-name search if no ScreenGroupComponent exists for that screen yet, so the
        // API isn't a no-op before a project adopts the convention. Returns an empty
        // Entity if nothing matches. This is the engine-side half of the
        // Behaviour::FindEntityInTopScreen/FindEntityInBottomScreen scripting API.
        Entity FindEntityInScreen(Screen screen, const std::string& name);

        // Walks `entity` and its Parent chain looking for a ScreenGroupComponent or
        // CameraComponent (entity itself checked first), returning true and filling
        // outScreen with the first one found. This is "true 2 worlds" separation for
        // rendering: SceneRenderer::RenderScreen and ScenePanel's split-view panes
        // use this to skip drawing an entity tagged for the OTHER screen entirely,
        // rather than only filtering by position-relative-to-camera as before.
        // Returns false if no tag exists anywhere in the chain (Ungrouped), in which
        // case the caller falls back to that older position-based behavior on either
        // screen -- untagged/legacy content keeps working exactly as before.
        bool TryResolveEntityScreen(Entity entity, Screen& outScreen);

        // Unity's GameObject.activeInHierarchy -- true only if `entity`'s own
        // ActiveComponent::Active is true AND every ancestor's is too (walks the Parent
        // chain the same way GetWorldTransform does). A disabled parent implicitly
        // disables the whole subtree even though each child's own Active flag is
        // untouched -- re-enabling the parent later restores each child to whatever its
        // own flag says, matching Unity exactly.
        bool IsEffectivelyActive(Entity entity);

        // Runtime (Play mode) lifecycle -- instantiates/updates/destroys
        // every entity's BehaviourComponent, if any.
        void OnRuntimeStart();
        void OnRuntimeUpdate(float deltaTime);
        void OnRuntimeStop();

        entt::registry& Registry() { return m_Registry; }

    private:
        // Returns m_RootEntities when parent is null, else parent's own
        // HierarchyComponent::Children -- SetParent/DestroyEntity both operate on
        // "the sibling list a given parent owns" and this lets them treat the
        // (parent-less) root list and a real parent's Children list identically.
        std::vector<Entity>& SiblingListFor(Entity parent);

        entt::registry m_Registry;
        std::vector<Entity> m_RootEntities;

        // Opaque (actually b2World*) so this header doesn't need to include
        // Box2D -- only non-null between OnRuntimeStart and OnRuntimeStop.
        void* m_PhysicsWorld = nullptr;

        // Opaque (actually a small owning bundle of btDiscreteDynamicsWorld +
        // its collision configuration/dispatcher/broadphase/solver, see
        // Physics3DWorld in Scene.cpp) so this header doesn't need to include
        // Bullet -- only non-null between OnRuntimeStart and OnRuntimeStop,
        // same lifetime convention as m_PhysicsWorld above.
        void* m_PhysicsWorld3D = nullptr;
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
