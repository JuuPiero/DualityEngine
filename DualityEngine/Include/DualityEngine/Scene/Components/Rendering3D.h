#pragma once

// 3D rendering components: Mesh Filter + Mesh Renderer (Unity's split), Skinned Mesh Renderer,
// the lights, LOD Group and Billboard. Part of Scene/Components.h.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // Unity's MeshFilter: which mesh the entity's MeshRenderer draws (and the default shape of
    // its Mesh Collider). Mesh is an imported mesh/model asset guid or a built-in mesh
    // "builtin:Cube" / "builtin:Sphere" / ... (Renderer/BuiltinMeshes.h). Empty = nothing is
    // drawn, like a MeshFilter with Mesh = None in Unity. Transform Scale sizes the mesh.
    struct MeshFilterComponent {
        AssetRef Mesh;
    };

    // Unity's MeshRenderer: draws the MeshFilter's mesh on the same entity with Materials.
    // Without a MeshFilter (or with an empty/missing mesh) it draws nothing. Only drawn on a
    // screen whose primary camera renders 3D -- see CameraComponent.
    //
    // Scenes saved before the MeshFilter split stored "Primitive"/"Mesh" in "Mesh Renderer";
    // EntitySerialization migrates them into a Mesh Filter when loading.
    struct MeshRendererComponent {
        bool Enabled = true;
        // One ".mat" asset guid (see Asset/Material.h) per submesh, resolved via MaterialLoader
        // at render time -- index-matched against the mesh's own part ranges (MeshLoader.h's
        // MeshData::SubMesh, split on the imported ".obj"'s "usemtl" / "o" / "g" boundaries).
        // Slot i paints submesh i only. A single entry still covers the whole mesh (one look).
        // Two or more entries do not repeat the last slot onto leftover parts -- those stay
        // the default white material. An empty list (or an unresolved/empty guid in a slot)
        // falls back to that same default. A built-in mesh has one submesh (index 0).
        std::vector<AssetRef> Materials;
        // Runtime material-property override, deliberately excluded from reflection and scene
        // serialization. Behaviour code can animate one renderer's Material Color without
        // modifying the shared .mat asset or requiring a compiled engine symbol across the
        // hot-reload GameScripts DLL boundary. Texture and ShadingMode still come from the
        // assigned material; when enabled this value replaces that material's Color property.
        // This is the first, color-only slice of a future MaterialPropertyBlock/MaterialInstance.
        bool HasRuntimeMaterialColor = false;
        glm::vec4 RuntimeMaterialColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        int SortOrder = 0;
    };

    // CPU-skinned model path. Bones is ordered exactly as MeshData::Bones (the imported
    // bone list shown by the model sub-asset); each entry references a scene entity carrying
    // that bone's animated Transform. RuntimeMeshHandle is renderer-owned and not serialized.
    struct SkinnedMeshRendererComponent {
        bool Enabled = true;
        AssetRef Mesh;
        std::vector<AssetRef> Materials;
        std::vector<EntityRef> Bones;
        int SortOrder = 0;
        // Renderer-owned caches: the animated pose is calculated at most once for an unchanged
        // skeleton, then shared by Top/Bottom/Scene rendering. They are deliberately omitted
        // from reflection/serialization.
        std::vector<glm::mat4> RuntimeSkinMatrices;
        std::vector<glm::mat4> RuntimePaletteMatrices;
        std::string RuntimeMeshPath;
        uint64_t RuntimePoseSignature = 0;
        uint64_t RuntimeDynamicPoseSignature = 0;
        uint32_t RuntimeMeshHandle = 0;
    };

    // Slot i paints submesh i. One assigned material still covers every part; extra parts
    // beyond the list stay default instead of inheriting the last slot (that repeat made
    // material 2, then 3, look like they overpainted the whole mesh).
    inline const AssetRef& MaterialForSubMesh(const std::vector<AssetRef>& materials, uint32_t subMeshIndex) {
        static const AssetRef none{};
        if (materials.empty())
            return none;
        if (materials.size() == 1)
            return materials[0];
        if (subMeshIndex < materials.size())
            return materials[subMeshIndex];
        return none;
    }

    // One enabled directional light is selected for each RenderView. Its direction derives
    // from this entity's world rotation; color/intensity stay authored data.
    struct DirectionalLightComponent {
        bool Enabled = true;
        glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;
        // Enables the current low-cost projected blob-shadow tier for this selected main light.
        // It is intentionally false by default so existing scenes retain their exact visuals.
        bool CastShadows = false;
    };

    // Omnidirectional light at the entity's world position (Unity Light, Type = Point).
    // Lights Lit/* and Toon/* materials only (Mobile/VertexLit and Unlit ignore it). Falloff
    // reaches exactly zero at Range (LightingMath::DistanceAttenuation -- the same curve the
    // 3DS samples into its distance-attenuation LUT). Color * Intensity saturates at 1 per
    // channel on both platforms (PICA200 light color registers are 8-bit).
    struct PointLightComponent {
        bool Enabled = true;
        glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;
        float Range = 10.0f;
    };

    // Cone light pointing along the entity's forward axis (-Z, like a Camera), Unity Light
    // Type = Spot. Spot Angle is the FULL cone angle in degrees (Unity convention); light fades
    // from Inner Spot Angle to Spot Angle (PICA spotlight LUT on 3DS, smoothstep on desktop).
    struct SpotLightComponent {
        bool Enabled = true;
        glm::vec3 Color{ 1.0f, 1.0f, 1.0f };
        float Intensity = 1.0f;
        float Range = 10.0f;
        float SpotAngle = 30.0f;
        float InnerSpotAngle = 20.0f;
    };

    enum class BillboardMode { FaceCamera, AlignWithCamera };

    // Rotates the entity every frame (Play) to face the screen's primary camera.
    struct BillboardComponent {
        bool Enabled = true;
        BillboardMode Mode = BillboardMode::AlignWithCamera;
        bool LockYAxis = false;
    };

    // Distance-based LOD switch (cheap on 3DS): LOD n is shown while the camera is closer than
    // Distance n (LOD3 beyond Distance 2), everything is hidden beyond Cull Distance (0 = never).
    // Each LOD is an entity whose whole subtree is shown/hidden; unassigned slots are skipped.
    struct LODGroupComponent {
        bool Enabled = true;
        EntityRef LOD0, LOD1, LOD2, LOD3;
        float Distance0 = 10.0f;
        float Distance1 = 25.0f;
        float Distance2 = 50.0f;
        float CullDistance = 0.0f;
        float LODBias = 1.0f;
        // Runtime: level currently shown (-1 = culled, -2 = not evaluated yet).
        int CurrentLOD = -2;
    };

    // Empty marker added/removed by the LOD system (never registered, never serialized): the
    // mesh passes skip renderers carrying it.
    struct LODHiddenTag {};

}
