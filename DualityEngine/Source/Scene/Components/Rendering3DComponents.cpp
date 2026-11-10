// Registration of Scene/Components/Rendering3D.h. Display and field names are serialized: treat
// them as a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterRendering3DComponents() {
        Component<MeshFilterComponent>("Mesh Filter", ComponentCategory::Rendering, {
            MeshAsset(Tip(MakeField("Mesh", &MeshFilterComponent::Mesh),
                "The mesh drawn by the Mesh Renderer: a built-in mesh (Cube, Sphere...) or an imported mesh/model.")),
        });

        Component<MeshRendererComponent>("Mesh Renderer", ComponentCategory::Rendering, {
            MakeField("Enabled", &MeshRendererComponent::Enabled),
            MakeField("Materials", &MeshRendererComponent::Materials),
            MakeField("Sort Order", &MeshRendererComponent::SortOrder),
        });

        Component<SkinnedMeshRendererComponent>("Skinned Mesh Renderer", ComponentCategory::Rendering, {
            MakeField("Enabled", &SkinnedMeshRendererComponent::Enabled),
            MakeField("Mesh", &SkinnedMeshRendererComponent::Mesh),
            MakeField("Materials", &SkinnedMeshRendererComponent::Materials),
            MakeField("Bones", &SkinnedMeshRendererComponent::Bones),
            MakeField("Sort Order", &SkinnedMeshRendererComponent::SortOrder),
        });

        Component<DirectionalLightComponent>("Directional Light", ComponentCategory::Rendering, {
            MakeField("Enabled", &DirectionalLightComponent::Enabled),
            MakeField("Color", &DirectionalLightComponent::Color),
            MakeField("Intensity", &DirectionalLightComponent::Intensity),
            MakeField("Cast Shadows", &DirectionalLightComponent::CastShadows),
        });

        Component<PointLightComponent>("Point Light", ComponentCategory::Rendering, {
            MakeField("Enabled", &PointLightComponent::Enabled),
            MakeField("Color", &PointLightComponent::Color),
            Ranged(MakeField("Intensity", &PointLightComponent::Intensity), 0.0f, 4.0f,
                "Color x Intensity saturates at 1 per channel (PICA200 8-bit light registers)."),
            Tip(MakeField("Range", &PointLightComponent::Range),
                "World units; light fades to exactly 0 at this distance."),
        });

        Component<SpotLightComponent>("Spot Light", ComponentCategory::Rendering, {
            MakeField("Enabled", &SpotLightComponent::Enabled),
            MakeField("Color", &SpotLightComponent::Color),
            Ranged(MakeField("Intensity", &SpotLightComponent::Intensity), 0.0f, 4.0f,
                "Color x Intensity saturates at 1 per channel (PICA200 8-bit light registers)."),
            Tip(MakeField("Range", &SpotLightComponent::Range),
                "World units; light fades to exactly 0 at this distance."),
            Ranged(MakeField("Spot Angle", &SpotLightComponent::SpotAngle), 1.0f, 179.0f,
                "Full cone angle in degrees (points along the entity's -Z axis)."),
            Ranged(MakeField("Inner Spot Angle", &SpotLightComponent::InnerSpotAngle), 0.0f, 179.0f,
                "Full angle of the fully-lit core; light fades from here to Spot Angle."),
        });

        Component<BillboardComponent>("Billboard", ComponentCategory::Rendering, {
            MakeField("Enabled", &BillboardComponent::Enabled),
            MakeEnumField("Mode", &BillboardComponent::Mode, { "Face Camera", "Align With Camera" }),
            Tip(MakeField("Lock Y Axis", &BillboardComponent::LockYAxis), "Only turn around the vertical axis (trees, sprites on the ground)."),
        });

        Component<LODGroupComponent>("LOD Group", ComponentCategory::Rendering, {
            MakeField("Enabled", &LODGroupComponent::Enabled),
            Tip(MakeField("LOD 0", &LODGroupComponent::LOD0), "Entity (with its children) shown closest to the camera."),
            MakeField("LOD 1", &LODGroupComponent::LOD1),
            MakeField("LOD 2", &LODGroupComponent::LOD2),
            MakeField("LOD 3", &LODGroupComponent::LOD3),
            Tip(MakeField("Distance 0", &LODGroupComponent::Distance0), "LOD 0 -> LOD 1 switch distance (world units)."),
            Tip(MakeField("Distance 1", &LODGroupComponent::Distance1), "LOD 1 -> LOD 2."),
            Tip(MakeField("Distance 2", &LODGroupComponent::Distance2), "LOD 2 -> LOD 3."),
            Tip(MakeField("Cull Distance", &LODGroupComponent::CullDistance), "Hide everything beyond this distance (0 = never)."),
            Ranged(MakeField("LOD Bias", &LODGroupComponent::LODBias), 0.1f, 4.0f, "Multiplies every distance (> 1 keeps detail longer)."),
        });
    }

}
