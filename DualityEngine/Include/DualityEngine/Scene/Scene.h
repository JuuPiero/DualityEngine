#pragma once

#include <string>
#include <vector>

#include <entt.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Physics/RaycastHit.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Layer.h"

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

        // The optional organizational roots for the 3DS's physical screens.
        // A child inherits its effective Layer from Top/Bottom through the
        // normal parent walk, so it does not need a LayerComponent itself.
        Entity GetScreenRoot(Screen screen);
        Entity EnsureScreenRoot(Screen screen);

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

        // Searches every LayerComponent{TOP/BOTTOM} entity's subtree (any depth, via
        // HierarchyComponent::Children) for one named `name`. Falls back to a scene-wide
        // by-name search if no layer root exists for that screen yet, so the
        // API isn't a no-op before a project adopts the convention. Returns an empty
        // Entity if nothing matches. This is the engine-side half of the
        // ScriptScene::FindEntityInTopScreen/FindEntityInBottomScreen scripting API.
        Entity FindEntityInScreen(Screen screen, const std::string& name);

        // Walks `entity` and its Parent chain looking for a LayerComponent or
        // CameraComponent, returning the resolved Layer value.
        Layer ResolveEntityLayer(Entity entity);

        // Walks `entity` and its Parent chain looking for a LayerComponent or
        // CameraComponent (entity itself checked first), returning true and filling
        // outScreen with the physical screen for Layer::TOP/BOTTOM (or a camera's own
        // Screen). Used by SceneRenderer::ShouldRenderOnScreen for "2 worlds" separation.
        // Returns false for Default layer (ungrouped) -- caller falls back to position-based
        // visibility on either screen.
        bool TryResolveEntityScreen(Entity entity, Screen& outScreen);

        // Unity's GameObject.activeInHierarchy -- true only if `entity`'s own
        // ActiveComponent::Active is true AND every ancestor's is too (walks the Parent
        // chain the same way GetWorldTransform does). A disabled parent implicitly
        // disables the whole subtree even though each child's own Active flag is
        // untouched -- re-enabling the parent later restores each child to whatever its
        // own flag says, matching Unity exactly.
        bool IsEffectivelyActive(Entity entity);

        // Unity's Physics2D.Raycast/Physics.Raycast -- casts a ray from `origin` along
        // (normalized) `direction` up to `maxDistance`, returning the CLOSEST collider hit (a
        // falsy/default RaycastHit2D/3D if nothing was, or if physics isn't running -- only
        // valid between OnRuntimeStart/OnRuntimeStop, same as every other RuntimeBody-reading
        // API). 2D only ever tests against Box2D colliders, 3D only against Bullet ones --
        // there's no cross-dimension ray (a 2D scene's colliders have no meaningful 3D
        // geometry to hit and vice versa).
        RaycastHit2D Raycast2D(const glm::vec2& origin, const glm::vec2& direction, float maxDistance);
        RaycastHit3D Raycast3D(const glm::vec3& origin, const glm::vec3& direction, float maxDistance);

        // Converts a point in `screen`'s own local pixel space (top-left origin, Y-down, e.g.
        // Behaviour::GetPointerPosition()) into a world-space ray from that screen's PRIMARY
        // camera, for "click/touch a 3D object" gameplay (feed the result straight into
        // Raycast3D). Returns false (leaving outOrigin/outDirection untouched) if that screen
        // has no primary camera, or its camera isn't Perspective -- an Orthographic camera's
        // "ray" would need parallel-projection handling this doesn't attempt.
        bool ScreenPointToRay3D(Screen screen, const glm::vec2& screenPoint, glm::vec3& outOrigin, glm::vec3& outDirection);
        bool ScreenPointToRay3D(Entity cameraEntity, const glm::vec2& screenPoint, glm::vec3& outOrigin, glm::vec3& outDirection);

        // Inverse of SceneRenderer's orthographic 2D projection for a specific camera entity.
        bool ScreenPointToWorld2D(Entity cameraEntity, const glm::vec2& screenPoint, glm::vec2& outWorld);

        // Closest Box2D fixture whose shape contains `worldPoint` (2D "click" query).
        RaycastHit2D RaycastPoint2D(const glm::vec2& worldPoint);

        // Runtime (Play mode) lifecycle -- instantiates/updates/destroys
        // every entity's BehaviourComponent, if any.
        void OnRuntimeStart();
        void OnRuntimeUpdate(float deltaTime);
        void OnRuntimeStop();

        // Platform hosts forward their lifecycle here on the main thread.
        // Scene dispatches safely to active script instances, so libctru/GLFW
        // callbacks never execute gameplay code directly.
        void OnApplicationFocus(bool focused);
        void OnApplicationPause(bool paused);
        void OnApplicationQuit();

        // Destroys every entity and resets root-entity bookkeeping, leaving this Scene as if
        // freshly default-constructed -- used before loading new content into an EXISTING
        // Scene object (Editor "Load Scene"/opening a different scene, Play->Stop reverting
        // to the pre-Play snapshot) so old entities don't pile up alongside the newly loaded
        // ones. A plain `m_Registry.clear()` alone isn't enough -- it would leave
        // `m_RootEntities` holding dangling handles into now-destroyed entities. Must not be
        // called between OnRuntimeStart/OnRuntimeStop (the physics worlds are still alive and
        // reference entities this would invalidate) -- callers stop Play first.
        void Clear();

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

        // Fixed-timestep accumulator (see OnRuntimeUpdate's own comment on why physics steps at
        // a constant 1/60s regardless of the real, variable frame deltaTime it's called with).
        float m_PhysicsAccumulator = 0.0f;
    };

    // --- Entity template method definitions -------------------------------
    // Defined here (after Scene is complete) rather than in Entity.h, since
    // they need Scene::Registry() to be a complete type.

    // Entity is deliberately a header-only handle from a game-script DLL's
    // point of view. Keeping this check inline prevents the DLL from needing
    // to import an engine implementation symbol just to test a handle.
    inline bool Entity::IsValid() const {
        return m_Scene && m_Handle != entt::null && m_Scene->Registry().valid(m_Handle);
    }

    template<typename T, typename... Args>
    T& Entity::AddComponent(Args&&... args) {
        return m_Scene->Registry().emplace<T>(m_Handle, std::forward<Args>(args)...);
    }

    template<typename T>
    T& Entity::GetComponent() {
        return m_Scene->Registry().get<T>(m_Handle);
    }

    template<typename T>
    const T& Entity::GetComponent() const {
        return m_Scene->Registry().get<T>(m_Handle);
    }

    template<typename T>
    T* Entity::TryGetComponent() {
        if (!IsValid() || !m_Scene->Registry().all_of<T>(m_Handle))
            return nullptr;
        return &m_Scene->Registry().get<T>(m_Handle);
    }

    template<typename T>
    const T* Entity::TryGetComponent() const {
        if (!IsValid() || !m_Scene->Registry().all_of<T>(m_Handle))
            return nullptr;
        return &m_Scene->Registry().get<T>(m_Handle);
    }

    template<typename T>
    bool Entity::HasComponent() const {
        return IsValid() && m_Scene->Registry().all_of<T>(m_Handle);
    }

    template<typename T>
    void Entity::RemoveComponent() {
        m_Scene->Registry().remove<T>(m_Handle);
    }

}
