// Registration of Scene/Components/Core.h. Display and field names are serialized: treat them as
// a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterCoreComponents() {
        // Name / Tag / Active / Layer / Prefab Instance are edited in the Inspector header.
        Mandatory<NameComponent>("Name", {
            MakeField("Name", &NameComponent::Name),
        }).HeaderComponent = true;

        Mandatory<TagComponent>("Tag", {
            MakeField("Tag", &TagComponent::Tag),
        }).HeaderComponent = true;

        Mandatory<ActiveComponent>("Active", {
            MakeField("Active", &ActiveComponent::Active),
        }).HeaderComponent = true;

        Mandatory<TransformComponent>("Transform", {
            MakeField("Translation", &TransformComponent::Translation),
            MakeField("Rotation", &TransformComponent::Rotation),
            MakeField("Scale", &TransformComponent::Scale),
        });

        Hidden<PrefabInstanceComponent>("Prefab Instance", {
            MakeField("Prefab", &PrefabInstanceComponent::Prefab),
        }).HeaderComponent = true;

        ComponentTypeInfo& camera = Component<CameraComponent>("Camera", ComponentCategory::Camera, {
            MakeField("Enabled", &CameraComponent::Enabled),
            MakeField("Screen", &CameraComponent::Screen),
            MakeField("Primary", &CameraComponent::Primary),
            MakeField("Zoom", &CameraComponent::Zoom),
            MakeField("Projection", &CameraComponent::Projection),
            MakeField("Fov Degrees", &CameraComponent::FovDegrees),
            MakeField("Near Plane", &CameraComponent::NearPlane),
            MakeField("Far Plane", &CameraComponent::FarPlane),
            MakeColorField("Background", &CameraComponent::Background),
            MakeField("Culling Mask", &CameraComponent::CullingMask),
        });
        // Adding a Camera also adds the matching Physics Raycaster (pointer events).
        camera.AddDefault = [](Entity entity) {
            if (!entity.HasComponent<CameraComponent>())
                entity.AddComponent<CameraComponent>();
            auto& component = entity.GetComponent<CameraComponent>();
            if (component.Projection == ProjectionType::Orthographic) {
                if (!entity.HasComponent<PhysicsRaycaster2DComponent>())
                    entity.AddComponent<PhysicsRaycaster2DComponent>();
            } else {
                if (!entity.HasComponent<PhysicsRaycaster3DComponent>())
                    entity.AddComponent<PhysicsRaycaster3DComponent>();
            }
        };

        // Edited through the Inspector header's Layer dropdown, but still in the Component menu.
        Component<LayerComponent>("Layer", ComponentCategory::Miscellaneous, {
            MakeField("Layer", &LayerComponent::Value),
        }).HeaderComponent = true;
    }

}
