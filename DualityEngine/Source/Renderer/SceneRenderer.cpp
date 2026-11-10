#include "DualityEngine/Renderer/SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/MeshReference.h"
#include "DualityEngine/Asset/SpriteDatabase.h"
#include "DualityEngine/Asset/TilemapAsset.h"
#include "DualityEngine/Core/Profiler.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Renderer/DrawHelpers2D.h"
#include "DualityEngine/Renderer/TrailTextLODRendering.h"
#include "DualityEngine/Renderer/UIWidgetRenderer.h"
#include "DualityEngine/Renderer/LightingMath.h"
#include "DualityEngine/Renderer/RenderSettings.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"

namespace Duality {

    namespace {
        struct SpriteDrawItem {
            entt::entity Handle;
            int SortOrder = 0;
            std::size_t HierarchyOrder = 0;
            bool IsTilemap = false;
        };

        glm::vec4 FrameUV(int frame, int columns, int rows) {
            if (columns <= 0 || rows <= 0)
                return { 0.0f, 0.0f, 1.0f, 1.0f };
            int col = frame % columns;
            int row = frame / columns;
            float uw = 1.0f / static_cast<float>(columns);
            float vh = 1.0f / static_cast<float>(rows);
            return { col * uw, row * vh, (col + 1) * uw, (row + 1) * vh };
        }

        // Which sprite an entity shows this frame: a Sprite Sheet Animator frame (grid cell, or
        // the N-th sliced sprite when its texture is a Multiple-mode sheet), else the flipbook
        // frame / SpriteRenderer texture (whole texture or one "guid#id" sprite).
        bool ResolveSpriteVisual(Scene& scene, entt::entity handle, ResolvedSprite& out) {
            if (scene.Registry().all_of<SpriteSheetAnimatorComponent>(handle)) {
                auto& anim = scene.Registry().get<SpriteSheetAnimatorComponent>(handle);
                if (anim.Enabled && !anim.Texture.Guid.empty()) {
                    const int sliced = SpriteRef::IsSubSprite(anim.Texture.Guid) ? 0 : SpriteDatabase::SpriteCount(anim.Texture.Guid);
                    if (sliced > 0)
                        return SpriteDatabase::ResolveIndex(anim.Texture.Guid, std::max(0, anim.CurrentFrame) % sliced, out);
                    if (!SpriteDatabase::Resolve(anim.Texture.Guid, out))
                        return false;
                    out.UV = ComposeUV(out.UV, FrameUV(anim.CurrentFrame, anim.Columns, anim.Rows));
                    if (anim.Columns > 0 && anim.Rows > 0)
                        out.SizePixels /= glm::vec2(static_cast<float>(anim.Columns), static_cast<float>(anim.Rows));
                    out.HasPivot = false;
                    out.BorderPixels = glm::vec4(0.0f);
                    return true;
                }
            }
            return SpriteDatabase::Resolve(GetActiveSpriteTexture(scene, handle).Guid, out);
        }

        glm::vec4 BorderUVFor(IRenderer2D& renderer, uint32_t textureId, const ResolvedSprite& sprite, const glm::vec4& borderPixels) {
            glm::vec2 textureSize = sprite.TextureSizePixels;
            if ((textureSize.x <= 0.0f || textureSize.y <= 0.0f) && textureId != 0)
                textureSize = renderer.GetTextureSize(textureId);
            if (textureSize.x > 0.0f && textureSize.y > 0.0f)
                return { borderPixels.x / textureSize.x, borderPixels.y / textureSize.x, borderPixels.z / textureSize.y, borderPixels.w / textureSize.y };
            const float w = sprite.UV.z - sprite.UV.x, h = sprite.UV.w - sprite.UV.y;
            return { 0.25f * w, 0.25f * w, 0.25f * h, 0.25f * h };
        }

        RenderView BuildRenderViewInternal(Scene& scene, Entity camera, Screen screen) {
            const CameraComponent& cameraComponent = camera.GetComponent<CameraComponent>();
            const TransformComponent cameraTransform = scene.GetWorldTransform(camera);
            const float screenWidth = screen == Screen::Top ? static_cast<float>(TopScreenWidth) : static_cast<float>(BottomScreenWidth);
            const float screenHeight = screen == Screen::Top ? static_cast<float>(TopScreenHeight) : static_cast<float>(BottomScreenHeight);

            RenderView view;
            view.TargetScreen = screen;
            view.Projection = cameraComponent.Projection;
            view.CameraPosition = cameraTransform.Translation;
            view.CameraRotationDegrees = cameraTransform.Rotation;
            view.FovDegrees = cameraComponent.FovDegrees;
            view.OrthoHalfHeight = screenHeight * 0.5f / cameraComponent.Zoom;
            view.AspectRatio = screenWidth / screenHeight;
            view.NearPlane = cameraComponent.NearPlane;
            view.FarPlane = cameraComponent.FarPlane;

            PopulateMainDirectionalLight(scene, view);
            return view;
        }

        glm::mat4 RotationMatrix(const glm::vec3& rotationDegrees) {
            glm::mat4 rotation(1.0f);
            rotation = glm::rotate(rotation, glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            rotation = glm::rotate(rotation, glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            rotation = glm::rotate(rotation, glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            return rotation;
        }

        // The mesh a MeshRenderer entity draws this frame (its MeshFilter's), or false when it
        // draws nothing (no MeshFilter / empty or missing mesh -- Unity draws nothing too).
        bool ResolveRendererMesh(IRenderer3D& renderer, Scene& scene, entt::entity handle, MeshPrimitive& primitive, uint32_t& meshHandle, const AssetRef** meshRef = nullptr) {
            const MeshFilterComponent* filter = scene.Registry().try_get<MeshFilterComponent>(handle);
            if (!filter || !ResolveMeshFilter(renderer, filter->Mesh, primitive, meshHandle))
                return false;
            if (meshRef)
                *meshRef = &filter->Mesh;
            return true;
        }

        // Radius of the projected blob shadow of a mesh, before Transform scale. An upright
        // capsule/cylinder (radius 0.25) gets its footprint rather than its bounding sphere;
        // imported meshes keep the unit-cube footprint they had before MeshFilter existed.
        float BlobShadowFootprintRadius(const AssetRef& mesh, const glm::vec3& scale) {
            const float maxScale = std::max({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
            MeshPrimitive primitive = MeshPrimitive::Cube;
            BuiltinMeshes::TryGetPrimitive(mesh, primitive);
            switch (primitive) {
                case MeshPrimitive::Capsule:
                case MeshPrimitive::Cylinder: return 0.25f * std::max(std::abs(scale.x), std::abs(scale.z));
                case MeshPrimitive::Cube:     return 0.5f * std::sqrt(3.0f) * maxScale;
                default:                      return 0.5f * maxScale;
            }
        }

        glm::mat4 TransformMatrix(const TransformComponent& transform) {
            glm::mat4 matrix = glm::translate(glm::mat4(1.0f), transform.Translation);
            matrix *= RotationMatrix(transform.Rotation);
            return glm::scale(matrix, transform.Scale);
        }

        uint64_t HashMatrix(uint64_t hash, const glm::mat4& matrix) {
            // A pose cache must not rely on a global frame counter: editor Scene/Game previews
            // can render the same scene through multiple cameras in one frame. Bit hashing the
            // already-calculated world transforms avoids re-skinning vertices for each view.
            for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row) {
                uint32_t bits = 0;
                std::memcpy(&bits, &matrix[column][row], sizeof(bits));
                hash ^= bits;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        void RefreshSkinPose(Scene& scene, Entity owner, SkinnedMeshRendererComponent& skin, const MeshData& source) {
            std::unordered_map<std::string, Entity> namedBones;
            for (Entity candidate : scene.GetHierarchyTraversalOrder())
                if (candidate.HasComponent<NameComponent>()) namedBones[candidate.GetComponent<NameComponent>().Name] = candidate;

            const glm::mat4 ownerWorld = TransformMatrix(scene.GetWorldTransform(owner));
            const glm::mat4 inverseOwner = glm::inverse(ownerWorld);
            uint64_t signature = HashMatrix(1469598103934665603ull, ownerWorld);
            std::vector<glm::mat4> matrices(source.Bones.size(), glm::mat4(1.0f));
            for (size_t i = 0; i < source.Bones.size(); ++i) {
                Entity bone;
                if (i < skin.Bones.size() && skin.Bones[i].Handle != EntityRef::Invalid) {
                    const entt::entity handle = static_cast<entt::entity>(skin.Bones[i].Handle);
                    if (scene.Registry().valid(handle)) bone = Entity(handle, &scene);
                }
                if (!bone) {
                    auto found = namedBones.find(source.Bones[i].Name);
                    if (found != namedBones.end()) bone = found->second;
                }
                if (bone) {
                    const glm::mat4 boneWorld = TransformMatrix(scene.GetWorldTransform(bone));
                    signature = HashMatrix(signature, boneWorld);
                    matrices[i] = inverseOwner * boneWorld * source.Bones[i].Offset;
                } else {
                    // Include the missing-bone state so adding a matching entity invalidates
                    // the pose on the next view without a scene-wide renderer reset.
                    signature ^= static_cast<uint64_t>(i + 1);
                    signature *= 1099511628211ull;
                }
            }
            if (skin.RuntimePoseSignature != signature || skin.RuntimeSkinMatrices.size() != matrices.size()) {
                skin.RuntimeSkinMatrices = std::move(matrices);
                skin.RuntimePoseSignature = signature;
            }
        }

        MeshData SkinMesh(const MeshData& source, const std::vector<glm::mat4>& matrices) {
            MeshData result = source;
            if (source.Bones.empty() || matrices.empty()) return result;
            for (size_t vertexIndex = 0; vertexIndex < result.Vertices.size(); ++vertexIndex) {
                MeshVertex& vertex = result.Vertices[vertexIndex];
                glm::vec4 position(0.0f), normal(0.0f); float total = 0.0f;
                for (int influence = 0; influence < 4; ++influence) {
                    const float weight = vertex.BoneWeights[influence];
                    const uint32_t index = vertexIndex < source.LegacyBoneIndices.size()
                        ? source.LegacyBoneIndices[vertexIndex][influence] : vertex.BoneIndices[influence];
                    if (weight <= 0.0f || index >= matrices.size()) continue;
                    position += matrices[index] * glm::vec4(vertex.Position, 1.0f) * weight;
                    normal += matrices[index] * glm::vec4(vertex.Normal, 0.0f) * weight;
                    total += weight;
                }
                if (total > 0.0f) { vertex.Position = glm::vec3(position) / total; vertex.Normal = glm::normalize(glm::vec3(normal)); }
            }
            return result;
        }
    }

    RenderView BuildRenderView(Scene& scene, Entity camera, Screen screen) {
        return BuildRenderViewInternal(scene, camera, screen);
    }

    void PopulateMainDirectionalLight(Scene& scene, RenderView& view) {
        PopulateSceneLighting(scene, view);
    }

    void PopulateSceneLighting(Scene& scene, RenderView& view) {
        view.MainLight = {};
        view.Lights.clear();
        view.MainLightIndex = -1;
        view.ShadowTechnique = RenderSettings::GetEffectiveShadowMode();
        view.AmbientColor = scene.GetLightingSettings().EffectiveAmbient();

        const auto saturatedColor = [](const glm::vec3& color, float intensity) {
            return glm::clamp(color * std::max(intensity, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
        };

        // Storage order is an EnTT implementation detail. Use canonical hierarchy order so the
        // Game renderer and the editor's free Scene camera make the same deterministic choice.
        std::vector<SceneLight> others;
        for (Entity light : scene.GetHierarchyTraversalOrder()) {
            if (!scene.IsEffectivelyActive(light))
                continue;
            if (light.HasComponent<DirectionalLightComponent>()) {
                const DirectionalLightComponent& component = light.GetComponent<DirectionalLightComponent>();
                if (component.Enabled) {
                    const TransformComponent transform = scene.GetWorldTransform(light);
                    const glm::mat4 rotation = RotationMatrix(transform.Rotation);
                    // Entity forward is -Z; lighting needs the direction from a point toward the light.
                    const glm::vec3 toLight = -glm::normalize(glm::vec3(rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                    SceneLight sceneLight;
                    sceneLight.Type = LightType::Directional;
                    sceneLight.Position = transform.Translation;
                    sceneLight.Direction = toLight;
                    sceneLight.Color = saturatedColor(component.Color, component.Intensity);
                    if (!view.MainLight.Enabled) {
                        view.MainLight.Enabled = true;
                        view.MainLight.Direction = toLight;
                        view.MainLight.Color = component.Color;
                        view.MainLight.Intensity = component.Intensity;
                        view.MainLight.CastShadows = component.CastShadows;
                        sceneLight.CastShadows = component.CastShadows;
                        view.Lights.insert(view.Lights.begin(), sceneLight);
                        view.MainLightIndex = 0;
                    } else {
                        others.push_back(sceneLight);
                    }
                }
            }
            if (light.HasComponent<PointLightComponent>()) {
                const PointLightComponent& component = light.GetComponent<PointLightComponent>();
                if (component.Enabled && component.Range > 0.0f) {
                    SceneLight sceneLight;
                    sceneLight.Type = LightType::Point;
                    sceneLight.Position = scene.GetWorldTransform(light).Translation;
                    sceneLight.Color = saturatedColor(component.Color, component.Intensity);
                    sceneLight.Range = component.Range;
                    others.push_back(sceneLight);
                }
            }
            if (light.HasComponent<SpotLightComponent>()) {
                const SpotLightComponent& component = light.GetComponent<SpotLightComponent>();
                if (component.Enabled && component.Range > 0.0f) {
                    const TransformComponent transform = scene.GetWorldTransform(light);
                    const glm::mat4 rotation = RotationMatrix(transform.Rotation);
                    SceneLight sceneLight;
                    sceneLight.Type = LightType::Spot;
                    sceneLight.Position = transform.Translation;
                    sceneLight.Direction = glm::normalize(glm::vec3(rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                    sceneLight.Color = saturatedColor(component.Color, component.Intensity);
                    sceneLight.Range = component.Range;
                    const float outer = std::clamp(component.SpotAngle, 1.0f, 179.0f);
                    const float inner = std::clamp(component.InnerSpotAngle, 0.0f, outer);
                    sceneLight.CosOuter = std::cos(glm::radians(outer * 0.5f));
                    sceneLight.CosInner = std::cos(glm::radians(inner * 0.5f));
                    others.push_back(sceneLight);
                }
            }
        }
        view.Lights.insert(view.Lights.end(), others.begin(), others.end());
    }

    uint8_t SelectLightsForBounds(const RenderView& view, const glm::vec3& center, float radius, uint8_t outIndices[MaxLightsPerDraw]) {
        struct Candidate { uint8_t Index; float Score; };
        Candidate candidates[256];
        int candidateCount = 0;
        const size_t lightCount = std::min<size_t>(view.Lights.size(), 255);
        for (size_t i = 0; i < lightCount; ++i) {
            const SceneLight& light = view.Lights[i];
            const float brightness = light.Color.r * 0.3f + light.Color.g * 0.59f + light.Color.b * 0.11f;
            if (brightness <= 0.0f)
                continue;
            float score = 0.0f;
            if (light.Type == LightType::Directional) {
                // Directional lights always win; the main light (index 0) sorts first.
                score = (static_cast<int>(i) == view.MainLightIndex ? 3.0e6f : 2.0e6f) + brightness;
            } else {
                const glm::vec3 offset = center - light.Position;
                const float distance = glm::length(offset);
                const float nearest = std::max(0.0f, distance - radius);
                if (nearest >= light.Range)
                    continue; // bounding sphere entirely outside the light's range
                if (light.Type == LightType::Spot && distance > radius) {
                    // Conservative cone test: widen the half-angle by the sphere's angular size.
                    const float halfAngle = std::acos(std::clamp(light.CosOuter, -1.0f, 1.0f));
                    const float slack = std::asin(std::clamp(radius / distance, 0.0f, 1.0f));
                    const float axisAngle = std::acos(std::clamp(glm::dot(offset / distance, light.Direction), -1.0f, 1.0f));
                    if (axisAngle > halfAngle + slack)
                        continue;
                }
                score = brightness * LightingMath::DistanceAttenuation(nearest / light.Range);
                if (score <= 0.0f)
                    continue;
            }
            candidates[candidateCount++] = { static_cast<uint8_t>(i), score };
        }
        const int keep = std::min(candidateCount, MaxLightsPerDraw);
        std::partial_sort(candidates, candidates + keep, candidates + candidateCount,
            [](const Candidate& a, const Candidate& b) { return a.Score > b.Score || (a.Score == b.Score && a.Index < b.Index); });
        for (int i = 0; i < keep; ++i)
            outIndices[i] = candidates[i].Index;
        return static_cast<uint8_t>(keep);
    }

    AssetRef GetActiveSpriteTexture(Scene& scene, entt::entity handle) {
        if (scene.Registry().all_of<SpriteFlipbookComponent>(handle)) {
            auto& flipbook = scene.Registry().get<SpriteFlipbookComponent>(handle);
            if (flipbook.Enabled) {
                AssetRef& frame = GetFlipbookFrame(flipbook, flipbook.CurrentFrame);
                if (!frame.Guid.empty())
                    return frame;
            }
        }
        return scene.Registry().get<SpriteRendererComponent>(handle).Texture;
    }

    uint32_t ResolveSpriteTexture(IRenderer2D& renderer, const AssetRef& textureRef) {
        if (textureRef.Guid.empty())
            return 0;
        std::string path = AssetDatabase::ResolvePath(textureRef.Guid);
        if (path.empty())
            return 0;
        return renderer.LoadTexture(path);
    }

    bool BuildSpriteRendererDrawData(IRenderer2D& renderer, Scene& scene, entt::entity handle,
        const glm::vec2& pivotDrawPosition, float drawUnitsPerWorldUnit, SpriteDrawData& out) {
        if (!scene.Registry().all_of<SpriteRendererComponent, TransformComponent>(handle))
            return false;
        const SpriteRendererComponent& sprite = scene.Registry().get<SpriteRendererComponent>(handle);
        const TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));

        ResolvedSprite resolved;
        const bool hasTexture = ResolveSpriteVisual(scene, handle, resolved);
        out = SpriteDrawData{};
        out.TextureId = hasTexture ? renderer.LoadTexture(resolved.TexturePath) : 0;
        out.UV = hasTexture ? resolved.UV : glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        out.Color = sprite.Color;
        out.PivotPosition = pivotDrawPosition;
        out.RotationDegrees = transform.Rotation.z;
        const glm::vec2 scale{ transform.Scale.x, transform.Scale.y };
        // SpriteRenderer::Size is the unscaled quad size; Transform.Scale is applied here (and
        // a negative scale mirrors, like Unity) so the Scale gizmo matches Game and Scene.
        out.Size = glm::abs(sprite.Size * scale) * drawUnitsPerWorldUnit;
        out.FlipX = sprite.FlipX != (scale.x * sprite.Size.x < 0.0f);
        out.FlipY = sprite.FlipY != (scale.y * sprite.Size.y < 0.0f);
        out.Pivot = hasTexture && resolved.HasPivot ? resolved.Pivot : sprite.Pivot;

        const bool legacyBorder = sprite.SliceBorder != glm::vec4(0.0f);
        const glm::vec4 borderPixels = legacyBorder ? sprite.SliceBorder : resolved.BorderPixels;
        out.Sliced = hasTexture && (legacyBorder || sprite.DrawMode == SpriteDrawMode::Sliced) && borderPixels != glm::vec4(0.0f);
        if (out.Sliced) {
            // A border keeps its source-pixel size in the world (Unity's sliced sprite): px ->
            // world units via the texture's PPU, then -> draw units, times the object's scale.
            const float ppu = resolved.PixelsPerUnit > 0.0f ? resolved.PixelsPerUnit : PhysicsUnits::PPU();
            const float fx = drawUnitsPerWorldUnit / ppu * std::fabs(scale.x);
            const float fy = drawUnitsPerWorldUnit / ppu * std::fabs(scale.y);
            out.BorderDraw = { borderPixels.x * fx, borderPixels.y * fx, borderPixels.z * fy, borderPixels.w * fy };
            out.BorderUV = BorderUVFor(renderer, out.TextureId, resolved, borderPixels);
        }
        return true;
    }

    glm::vec2 EffectiveSpritePivot(Scene& scene, entt::entity handle) {
        if (!scene.Registry().all_of<SpriteRendererComponent>(handle))
            return glm::vec2(0.5f);
        ResolvedSprite resolved;
        if (ResolveSpriteVisual(scene, handle, resolved) && resolved.HasPivot)
            return resolved.Pivot;
        return scene.Registry().get<SpriteRendererComponent>(handle).Pivot;
    }

    int DrawTilemap2D(IRenderer2D& renderer, Scene& scene, entt::entity handle, const glm::vec2& cameraPosition,
        float drawUnitsPerWorldUnit, const glm::vec2& drawOffset, const glm::vec4& tint) {
        if (!scene.Registry().all_of<TilemapComponent, TransformComponent>(handle))
            return 0;
        const TilemapComponent& tilemap = scene.Registry().get<TilemapComponent>(handle);
        const TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));
        static const TilemapData s_Empty;
        const TilemapData& data = tilemap.TileData.Guid.empty() ? s_Empty : TilemapAsset::Get(AssetDatabase::ResolvePath(tilemap.TileData.Guid));
        ResolvedSprite whole;
        const bool hasTileset = SpriteDatabase::Resolve(tilemap.Tileset.Guid, whole);
        const uint32_t tileTexture = hasTileset ? renderer.LoadTexture(whole.TexturePath) : 0;
        const int spriteCount = hasTileset ? SpriteDatabase::SpriteCount(tilemap.Tileset.Guid) : 0;
        const int w = data.Width > 0 ? data.Width : tilemap.GridWidth;
        const int h = data.Height > 0 ? data.Height : tilemap.GridHeight;
        const glm::vec2 size = tilemap.CellSize * drawUnitsPerWorldUnit;
        // Tile index = index into the tileset's sliced sprites (Tile Palette). UVs resolved once
        // per draw, not per tile -- a big map on 3DS stays one array lookup per tile.
        std::vector<glm::vec4> tileUVs;
        if (spriteCount > 0) {
            const TextureImportSettings& settings = SpriteDatabase::Settings(whole.TexturePath);
            tileUVs.resize(static_cast<size_t>(spriteCount));
            ResolvedSprite tileSprite;
            for (int i = 0; i < spriteCount; ++i) {
                SpriteDatabase::Describe(whole.TexturePath, settings, i, tileSprite);
                tileUVs[static_cast<size_t>(i)] = tileSprite.UV;
            }
        }
        int drawn = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int idx = y * w + x;
                if (idx >= static_cast<int>(data.Tiles.size()) || data.Tiles[idx] < 0)
                    continue;
                glm::vec4 uv = whole.UV;
                if (spriteCount > 0) {
                    if (data.Tiles[idx] >= spriteCount)
                        continue;
                    uv = tileUVs[static_cast<size_t>(data.Tiles[idx])];
                }
                const glm::vec2 worldPos{ transform.Translation.x + x * tilemap.CellSize.x, transform.Translation.y + y * tilemap.CellSize.y };
                const glm::vec2 center = (worldPos - cameraPosition) * drawUnitsPerWorldUnit + drawOffset;
                renderer.DrawQuad(center - size * 0.5f, size, tint, 0.0f, tileTexture, uv);
                ++drawn;
            }
        }
        return drawn;
    }

    uint32_t ResolveMeshTexture(IRenderer3D& renderer, const AssetRef& textureRef) {
        if (textureRef.Guid.empty())
            return 0;
        std::string path = AssetDatabase::ResolvePath(textureRef.Guid);
        if (path.empty())
            return 0;
        return renderer.LoadTexture(path);
    }

    Material ResolveMeshMaterial(const AssetRef& materialRef) {
        if (materialRef.Guid.empty())
            return Material{};
        std::string path = AssetDatabase::ResolvePath(materialRef.Guid);
        if (path.empty())
            return Material{};
        return MaterialLoader::Load(path);
    }

    uint32_t ResolveMeshGeometry(IRenderer3D& renderer, const AssetRef& meshRef) {
        if (meshRef.Guid.empty())
            return 0;
        std::string path = AssetDatabase::ResolvePath(meshRef.Guid);
        if (path.empty())
            return 0;
        return renderer.LoadMesh(path);
    }

    bool ResolveMeshFilter(IRenderer3D& renderer, const AssetRef& mesh, MeshPrimitive& primitive, uint32_t& meshHandle) {
        meshHandle = 0;
        primitive = MeshPrimitive::Cube;
        if (BuiltinMeshes::TryGetPrimitive(mesh, primitive))
            return true;
        meshHandle = ResolveMeshGeometry(renderer, mesh);
        return meshHandle != 0;
    }

    bool ShouldRenderOnScreen(Scene& scene, entt::entity handle, Screen screen, const CameraComponent* camera) {
        Layer layer = scene.ResolveEntityLayer(Entity(handle, &scene));

        if (camera) {
            uint32_t mask = camera->CullingMask;
            int layerIndex = static_cast<int>(layer);
            if (layerIndex >= 0 && layerIndex < LayerCount && !(mask & LayerBit(layerIndex)))
                return false;
        }

        Screen layerScreen;
        if (LayerToScreen(layer, layerScreen))
            return layerScreen == screen;
        return true;
    }

    static void ScreenExtents(Screen screen, float& outWidth, float& outHeight) {
        if (screen == Screen::Top) {
            outWidth = static_cast<float>(TopScreenWidth);
            outHeight = static_cast<float>(TopScreenHeight);
        } else {
            outWidth = static_cast<float>(BottomScreenWidth);
            outHeight = static_cast<float>(BottomScreenHeight);
        }
    }

    // Conservative bounding-sphere vs. view-volume test against a RenderView (so the editor's
    // free Scene camera and gameplay cameras share it). Camera local space uses -Z forward.
    // False positives are cheap; a false negative would pop visible geometry. It provides an
    // actual culling win without requiring per-mesh bounds buffers on a memory-limited 3DS.
    static bool IsSphereVisibleInView(const RenderView& view, const glm::vec3& center, float radius) {
        const glm::mat4 inverseCameraRotation = glm::inverse(RotationMatrix(view.CameraRotationDegrees));
        const glm::vec3 localPosition = glm::vec3(inverseCameraRotation * glm::vec4(center - view.CameraPosition, 0.0f));
        const float depth = -localPosition.z;
        if (depth + radius < view.NearPlane || depth - radius > view.FarPlane)
            return false;

        if (view.Projection == ProjectionType::Perspective) {
            if (depth < -radius)
                return false;
            const float halfHeight = std::tan(glm::radians(view.FovDegrees) * 0.5f) * std::max(depth, 0.0f);
            const float halfWidth = halfHeight * view.AspectRatio;
            return std::abs(localPosition.x) <= halfWidth + radius && std::abs(localPosition.y) <= halfHeight + radius;
        }

        const float halfHeight = view.OrthoHalfHeight;
        const float halfWidth = halfHeight * view.AspectRatio;
        return std::abs(localPosition.x) <= halfWidth + radius && std::abs(localPosition.y) <= halfHeight + radius;
    }

    static ShadowMapPass BuildDirectionalShadowMapPass(const RenderView& view) {
        const glm::mat4 cameraRotation = RotationMatrix(view.CameraRotationDegrees);
        const glm::vec3 cameraForward = glm::normalize(glm::vec3(cameraRotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        // A compact, camera-following orthographic region makes the experimental
        // map useful for gameplay geometry near the viewer without allocating a
        // huge precision-starved target. Static baked vertex colors remain the
        // correct solution for distant environment lighting on 3DS.
        const glm::vec3 center = view.CameraPosition + cameraForward * std::min(view.FarPlane * 0.25f, 24.0f);
        const glm::vec3 toLight = glm::normalize(view.MainLight.Direction);
        const glm::vec3 eye = center + toLight * 48.0f;
        const glm::vec3 up = std::abs(glm::dot(toLight, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f
            ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        ShadowMapPass pass;
        pass.ViewProjection = glm::ortho(-24.0f, 24.0f, -24.0f, 24.0f, 1.0f, 96.0f) * glm::lookAt(eye, center, up);
        return pass;
    }

    static uint32_t RenderDirectionalShadowMap(IRenderer3D& renderer, Scene& scene, Screen screen,
        RenderView& view, const CameraComponent& cameraFilter) {
        if (view.ShadowTechnique != ShadowMode::ShadowMapsExperimental || !view.MainLight.Enabled || !view.MainLight.CastShadows)
            return 0;
        view.ShadowMap = BuildDirectionalShadowMapPass(view);
        if (!renderer.BeginDirectionalShadowMap(view.ShadowMap))
            return 0;

        uint32_t submitted = 0;
        for (auto handle : scene.Registry().view<TransformComponent, MeshRendererComponent>()) {
            if (!ShouldRenderOnScreen(scene, handle, screen, &cameraFilter))
                continue;
            Entity entity(handle, &scene);
            const auto& mesh = entity.GetComponent<MeshRendererComponent>();
            if (!mesh.Enabled || !scene.IsEffectivelyActive(entity) || scene.Registry().all_of<LODHiddenTag>(handle))
                continue;
            MeshPrimitive primitive;
            uint32_t meshHandle = 0;
            if (!ResolveRendererMesh(renderer, scene, handle, primitive, meshHandle))
                continue;
            const TransformComponent transform = scene.GetWorldTransform(entity);
            const uint32_t subMeshCount = renderer.GetSubMeshCount(meshHandle);
            for (uint32_t i = 0; i < subMeshCount; ++i) {
                renderer.DrawDirectionalShadowCaster(MeshDrawCommand{ primitive, meshHandle, i,
                    transform.Translation, transform.Rotation, transform.Scale });
                submitted++;
            }
        }
        // V3 skinned meshes keep source vertices on the GPU. Submit the same local palette to
        // the desktop depth shader so an animated character's shadow matches its visible pose.
        // Citro3D declines this pass and continues using the existing blob-shadow fallback.
        for (auto handle : scene.Registry().view<TransformComponent, SkinnedMeshRendererComponent>()) {
            if (!ShouldRenderOnScreen(scene, handle, screen, &cameraFilter)) continue;
            Entity entity(handle, &scene);
            auto& skin = entity.GetComponent<SkinnedMeshRendererComponent>();
            if (!skin.Enabled || skin.Mesh.Guid.empty() || !scene.IsEffectivelyActive(entity)) continue;
            const std::string path = AssetDatabase::ResolvePath(skin.Mesh.Guid);
            if (path.empty()) continue;
            const MeshData& source = MeshLoader::Load(path);
            if (source.Vertices.empty()) continue;
            RefreshSkinPose(scene, entity, skin, source);
            const TransformComponent transform = scene.GetWorldTransform(entity);
            const uint32_t meshHandle = ResolveMeshGeometry(renderer, skin.Mesh);
            for (uint32_t i = 0; i < renderer.GetSubMeshCount(meshHandle); ++i) {
                const MeshData::SubMesh* subMesh = i < source.SubMeshes.size() ? &source.SubMeshes[i] : nullptr;
                if (subMesh && !subMesh->BonePalette.empty()) {
                    skin.RuntimePaletteMatrices.clear();
                    skin.RuntimePaletteMatrices.reserve(subMesh->BonePalette.size());
                    for (uint16_t bone : subMesh->BonePalette)
                        skin.RuntimePaletteMatrices.push_back(bone < skin.RuntimeSkinMatrices.size() ? skin.RuntimeSkinMatrices[bone] : glm::mat4(1.0f));
                    renderer.DrawDirectionalShadowCaster(MeshDrawCommand{ MeshPrimitive::Cube, meshHandle, i,
                        transform.Translation, transform.Rotation, transform.Scale, {}, 0, MaterialShadingMode::Unlit, false, true,
                        skin.RuntimePaletteMatrices.data(), static_cast<uint32_t>(skin.RuntimePaletteMatrices.size()) });
                    submitted++;
                } else if (source.Bones.empty()) {
                    renderer.DrawDirectionalShadowCaster(MeshDrawCommand{ MeshPrimitive::Cube, meshHandle, i,
                        transform.Translation, transform.Rotation, transform.Scale });
                    submitted++;
                }
            }
        }
        renderer.EndDirectionalShadowMap();
        view.HasShadowMap = submitted > 0;
        return submitted;
    }

    SceneRenderStats RenderScreen(IRenderer2D& renderer2D, IRenderer3D& renderer3D, Scene& scene, Screen screen, const glm::vec4& clearColor, bool clear) {
        SceneRenderStats stats;
        Entity camera = scene.GetPrimaryCamera(screen);

        glm::vec4 effectiveClearColor = camera ? camera.GetComponent<CameraComponent>().Background : clearColor;

        if (camera) {
            DUALITY_PROFILE_SCOPE("Render.3D");
            stats += RenderScreen3D(renderer3D, scene, screen, effectiveClearColor, clear);
        }

        DUALITY_PROFILE_SCOPE("Render.2D");
        // A camera's 3D pass already clears the base scene. With no camera, the 2D pass owns
        // that clear. Neither pass clears while an additive scene is compositing on top.
        renderer2D.BeginScene(screen, effectiveClearColor, clear && !camera);

        if (camera) {
            TransformComponent cameraTransform = scene.GetWorldTransform(camera);
            auto& cameraComponent = camera.GetComponent<CameraComponent>();
            float screenWidth, screenHeight;
            ScreenExtents(screen, screenWidth, screenHeight);
            // Gameplay world coordinates are Unity-style units. PPU belongs only at the
            // last 2D render boundary, where a world unit becomes physical screen pixels.
            const float worldToPixels = cameraComponent.Zoom * PhysicsUnits::PPU();

            // Equal SpriteRenderer Sort Order follows the visible Hierarchy, not EnTT's dense
            // component storage (which is free to reorder at any time). Earlier siblings draw
            // first; the last sibling therefore appears on top, as in Unity.
            const std::vector<Entity> hierarchy = scene.GetHierarchyTraversalOrder();
            std::unordered_map<entt::entity, std::size_t> hierarchyOrder;
            hierarchyOrder.reserve(hierarchy.size());
            for (std::size_t i = 0; i < hierarchy.size(); ++i)
                hierarchyOrder[hierarchy[i].Handle()] = i;

            std::vector<SpriteDrawItem> sprites;
            auto view = scene.Registry().view<TransformComponent, SpriteRendererComponent>();
            for (auto handle : view) {
                if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                    continue;
                if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                    continue;
                if (!view.get<SpriteRendererComponent>(handle).Enabled)
                    continue;
                const auto found = hierarchyOrder.find(handle);
                const std::size_t drawOrder = found == hierarchyOrder.end() ? hierarchy.size() : found->second;
                sprites.push_back({ handle, view.get<SpriteRendererComponent>(handle).SortOrder, drawOrder });
            }
            // Tilemaps sort together with sprites by Sort Order (Unity's TilemapRenderer
            // "Order in Layer"), so a background tilemap can sit behind characters.
            auto tileView = scene.Registry().view<TransformComponent, TilemapComponent>();
            for (auto handle : tileView) {
                if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                    continue;
                if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                    continue;
                const TilemapComponent& tilemap = tileView.get<TilemapComponent>(handle);
                if (!tilemap.Enabled)
                    continue;
                const auto found = hierarchyOrder.find(handle);
                const std::size_t drawOrder = found == hierarchyOrder.end() ? hierarchy.size() : found->second;
                sprites.push_back({ handle, tilemap.SortOrder, drawOrder, true });
            }
            std::sort(sprites.begin(), sprites.end(), [](const SpriteDrawItem& a, const SpriteDrawItem& b) {
                if (a.SortOrder != b.SortOrder)
                    return a.SortOrder < b.SortOrder;
                if (a.HierarchyOrder != b.HierarchyOrder)
                    return a.HierarchyOrder < b.HierarchyOrder;
                return a.IsTilemap < b.IsTilemap;
            });

            for (auto& item : sprites) {
                if (item.IsTilemap) {
                    DUALITY_PROFILE_SCOPE("Render2D.Tilemap");
                    DrawTilemap2D(renderer2D, scene, item.Handle, { cameraTransform.Translation.x, cameraTransform.Translation.y },
                        worldToPixels, { screenWidth * 0.5f, screenHeight * 0.5f });
                    continue;
                }
                TransformComponent transform = scene.GetWorldTransform(Entity(item.Handle, &scene));
                const glm::vec2 screenPivot{
                    (transform.Translation.x - cameraTransform.Translation.x) * worldToPixels + screenWidth * 0.5f,
                    (transform.Translation.y - cameraTransform.Translation.y) * worldToPixels + screenHeight * 0.5f
                };
                // Shared with the editor Scene view (same pivot / sub-sprite / 9-slice / flip
                // rules), see BuildSpriteRendererDrawData.
                SpriteDrawData drawData;
                if (BuildSpriteRendererDrawData(renderer2D, scene, item.Handle, screenPivot, worldToPixels, drawData))
                    DrawSprite2D(renderer2D, drawData);
                stats.VisibleSprites++;
            }

            auto lineView = scene.Registry().view<TransformComponent, LineRendererComponent>();
            for (auto handle : lineView) {
                if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                    continue;
                if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                    continue;
                auto& line = lineView.get<LineRendererComponent>(handle);
                if (!line.Enabled)
                    continue;
                TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));
                glm::vec2 points[8];
                int count = std::clamp(line.PointCount, 0, 8);
                for (int i = 0; i < count; i++) {
                    glm::vec3 p = GetLinePoint(line, i);
                    if (!line.UseWorldSpace) {
                        // 2D LineRenderer follows the entity's complete world transform,
                        // including parent scale/rotation, instead of the previous translation-
                        // only approximation. Z remains present in the API for Unity-style data
                        // compatibility but this 2D renderer intentionally projects onto XY.
                        p.x *= transform.Scale.x;
                        p.y *= transform.Scale.y;
                        const float radians = glm::radians(transform.Rotation.z);
                        const float x = p.x * std::cos(radians) - p.y * std::sin(radians);
                        const float y = p.x * std::sin(radians) + p.y * std::cos(radians);
                        p.x = x + transform.Translation.x;
                        p.y = y + transform.Translation.y;
                    }
                    points[i] = {
                        (p.x - cameraTransform.Translation.x) * worldToPixels + screenWidth * 0.5f,
                        (p.y - cameraTransform.Translation.y) * worldToPixels + screenHeight * 0.5f
                    };
                }
                DrawLineStrip2D(renderer2D, points, count, line.Width * worldToPixels, line.Color, line.Loop);
            }

            auto particleView = scene.Registry().view<ParticleSystemComponent>();
            for (auto handle : particleView) {
                if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                    continue;
                auto& sys = particleView.get<ParticleSystemComponent>(handle);
                if (!sys.Enabled)
                    continue;
                uint32_t tex = ResolveSpriteTexture(renderer2D, sys.Texture);
                ResolvedSprite particleSprite; // a "guid#id" sprite texture draws its sub-rect
                const glm::vec4 particleUV = SpriteDatabase::Resolve(sys.Texture.Guid, particleSprite) ? particleSprite.UV : glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
                for (const Particle& p : GetParticlePool(handle)) {
                    glm::vec2 topLeft{
                        (p.Position.x - cameraTransform.Translation.x) * worldToPixels + screenWidth * 0.5f - p.Size * worldToPixels * 0.5f,
                        (p.Position.y - cameraTransform.Translation.y) * worldToPixels + screenHeight * 0.5f - p.Size * worldToPixels * 0.5f
                    };
                    renderer2D.DrawQuad(topLeft, { p.Size * worldToPixels, p.Size * worldToPixels }, p.Color, 0.0f, tex, particleUV);
                }
            }

            // Trail Renderer + Text Mesh (ExtendedRendering.cpp).
            RenderWorldExtras2D(renderer2D, scene, screen, &cameraComponent,
                { cameraTransform.Translation.x, cameraTransform.Translation.y }, worldToPixels,
                { screenWidth * 0.5f, screenHeight * 0.5f });
        }

        {
            DUALITY_PROFILE_SCOPE("Render.UI");
            RenderScreenUI(renderer2D, scene, screen);
        }

        renderer2D.EndScene();
        return stats;
    }

    uint32_t RenderDirectionalBlobShadows(IRenderer3D& renderer, Scene& scene, Screen screen, const RenderView& view, const CameraComponent* cameraFilter) {
        if (view.HasShadowMap || view.ShadowTechnique == ShadowMode::Off || !view.MainLight.Enabled || !view.MainLight.CastShadows)
            return 0;

        // RenderView stores the direction from a shaded point TOWARD the light. A projected
        // shadow travels the other way, from the caster toward the receiver plane.
        const float lightDirectionLength = glm::length(view.MainLight.Direction);
        if (lightDirectionLength <= 0.0001f)
            return 0;
        const glm::vec3 rayDirection = -view.MainLight.Direction / lightDirectionLength;

        struct Receiver {
            TransformComponent Transform;
            glm::vec3 Normal;
        };
        std::vector<Receiver> receivers;
        for (auto handle : scene.Registry().view<TransformComponent, MeshRendererComponent>()) {
            if (!ShouldRenderOnScreen(scene, handle, screen, cameraFilter))
                continue;
            Entity entity(handle, &scene);
            const auto& mesh = entity.GetComponent<MeshRendererComponent>();
            const MeshFilterComponent* filter = scene.Registry().try_get<MeshFilterComponent>(handle);
            MeshPrimitive primitive;
            const bool isPlane = filter && BuiltinMeshes::TryGetPrimitive(filter->Mesh, primitive) && primitive == MeshPrimitive::Plane;
            if (!mesh.Enabled || !isPlane || !scene.IsEffectivelyActive(entity))
                continue;
            TransformComponent transform = scene.GetWorldTransform(entity);
            const glm::vec3 normal = glm::normalize(glm::vec3(RotationMatrix(transform.Rotation) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
            if (std::abs(glm::dot(rayDirection, normal)) > 0.001f)
                receivers.push_back({ transform, normal });
        }
        if (receivers.empty())
            return 0;

        // A small translucent projected quad is the intentionally hardware-safe shadow tier:
        // no depth target, texture allocation or second camera pass. It looks best on a large
        // horizontal Plane ground receiver; rotated planes also work because the intersection
        // and shadow quad both use the receiver's world-space normal/rotation.
        constexpr glm::vec4 ShadowColor{ 0.015f, 0.02f, 0.04f, 0.38f };
        uint32_t submitted = 0;
        for (auto handle : scene.Registry().view<TransformComponent, MeshRendererComponent>()) {
            if (!ShouldRenderOnScreen(scene, handle, screen, cameraFilter))
                continue;
            Entity caster(handle, &scene);
            const auto& mesh = caster.GetComponent<MeshRendererComponent>();
            if (!mesh.Enabled || !scene.IsEffectivelyActive(caster) || scene.Registry().all_of<LODHiddenTag>(handle))
                continue;
            MeshPrimitive casterPrimitive;
            uint32_t casterMeshHandle = 0;
            const AssetRef* casterMesh = nullptr;
            if (!ResolveRendererMesh(renderer, scene, handle, casterPrimitive, casterMeshHandle, &casterMesh))
                continue;
            if (casterMeshHandle == 0 && casterPrimitive == MeshPrimitive::Plane)
                continue; // ground planes receive, they don't cast
            const TransformComponent casterTransform = scene.GetWorldTransform(caster);
            const float footprintRadius = BlobShadowFootprintRadius(*casterMesh, casterTransform.Scale);
            const float shadowDiameter = std::max(0.02f, footprintRadius * 2.0f);

            for (const Receiver& receiver : receivers) {
                const float denominator = glm::dot(rayDirection, receiver.Normal);
                const float rayDistance = glm::dot(receiver.Transform.Translation - casterTransform.Translation, receiver.Normal) / denominator;
                if (!std::isfinite(rayDistance) || rayDistance <= 0.0f)
                    continue; // receiver is behind the caster/light ray

                MeshDrawCommand shadow;
                shadow.Primitive = MeshPrimitive::Plane;
                shadow.Translation = casterTransform.Translation + rayDirection * rayDistance + receiver.Normal * (0.002f * std::max(1.0f, shadowDiameter));
                shadow.RotationDegrees = receiver.Transform.Rotation;
                shadow.Scale = { shadowDiameter, 1.0f, shadowDiameter };
                shadow.Color = ShadowColor;
                shadow.ShadingMode = MaterialShadingMode::Unlit;
                shadow.AlphaBlend = true;
                shadow.DepthWrite = false;
                renderer.DrawMesh(shadow);
                submitted++;
            }
        }
        return submitted;
    }

    RenderMaterial BuildRenderMaterial(IRenderer3D& renderer, const Material& material, uint32_t& outTextureId) {
        const ShaderInfo& info = material.GetShaderInfo();
        RenderMaterial result;
        result.Shader = material.Shader;
        result.Model = info.Model;
        result.Surface = material.EffectiveSurface();
        result.Cull = material.Cull;
        result.ZWrite = material.EffectiveZWrite();
        result.UseVertexColor = info.UsesVertexColor;
        result.UseTexture = info.UsesTexture;
        result.MainTexST = material.MainTexST;
        result.Cutoff = material.Cutoff;
        outTextureId = info.UsesTexture ? ResolveMeshTexture(renderer, material.Texture) : 0;
        if (info.Properties & ShaderProp_BumpMap)
            result.NormalTextureId = ResolveMeshTexture(renderer, material.NormalMap);
        if (info.Properties & ShaderProp_Specular) {
            result.SpecColor = glm::clamp(glm::vec3(material.SpecColor), glm::vec3(0.0f), glm::vec3(1.0f));
            result.SpecExponent = LightingMath::ShininessToExponent(material.Shininess);
        }
        if (info.Properties & ShaderProp_Emission)
            result.Emission = glm::clamp(glm::vec3(material.EmissionColor), glm::vec3(0.0f), glm::vec3(1.0f));
        if (info.Properties & ShaderProp_Toon) {
            result.ShadeColor = glm::clamp(glm::vec3(material.ShadeColor), glm::vec3(0.0f), glm::vec3(1.0f));
            result.ToonSteps = static_cast<float>(material.ToonSteps);
            result.ToonSmoothness = material.ToonSmoothness;
        }
        return result;
    }

    namespace {
        struct MeshDrawItem {
            MeshDrawCommand Command;
            int Queue = 2000;
            float Depth = 0.0f;       // view-space distance along the camera forward axis
            uint64_t StateKey = 0;    // groups identical GPU state (PICA LUT/TEV changes are costly)
        };

        uint64_t MaterialStateKey(const MeshDrawCommand& command) {
            const RenderMaterial& material = command.Material;
            uint64_t key = static_cast<uint64_t>(material.Shader) << 56;
            key |= static_cast<uint64_t>(command.SkinMatrices != nullptr) << 55;
            key |= static_cast<uint64_t>(command.TextureId & 0xFFFF) << 32;
            key |= static_cast<uint64_t>(material.NormalTextureId & 0xFFFF) << 16;
            key |= static_cast<uint64_t>(static_cast<uint32_t>(material.SpecExponent) & 0xFF) << 8;
            key |= static_cast<uint64_t>(static_cast<uint32_t>(material.ToonSteps) & 0xF) << 4;
            key |= static_cast<uint64_t>(material.Cull) & 0x3;
            return key;
        }

        bool UsesPerPixelLights(LightingModel model) {
            return model == LightingModel::Lambert || model == LightingModel::BlinnPhong || model == LightingModel::Toon;
        }
    }

    SceneRenderStats RenderMeshPass(IRenderer3D& renderer, Scene& scene, Screen screen, const RenderView& view, const MeshPassOptions& options) {
        SceneRenderStats stats;
        stats.ActiveLights = static_cast<uint32_t>(view.Lights.size());
        // LOD Groups pick their level for this view's camera (tags the hidden renderers).
        ApplyLODGroups(scene, view.CameraPosition);

        const glm::vec3 cameraForward = glm::normalize(glm::vec3(RotationMatrix(view.CameraRotationDegrees) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        std::vector<MeshDrawItem> items;
        // Skinned palettes must outlive sorting: one stable buffer per submesh draw.
        std::deque<std::vector<glm::mat4>> palettes;

        const auto finishCommand = [&](MeshDrawItem& item, const glm::vec3& center, float radius) {
            MeshDrawCommand& command = item.Command;
            if (UsesPerPixelLights(command.Material.Model))
                command.LightCount = SelectLightsForBounds(view, center, radius, command.LightIndices);
            item.Depth = glm::dot(center - view.CameraPosition, cameraForward);
            item.StateKey = MaterialStateKey(command);
        };

        auto view3D = scene.Registry().view<TransformComponent, MeshRendererComponent>();
        for (auto handle : view3D) {
            if (!ShouldRenderOnScreen(scene, handle, screen, options.CameraFilter))
                continue;
            Entity entity(handle, &scene);
            if (!scene.IsEffectivelyActive(entity))
                continue;
            auto& mesh = view3D.get<MeshRendererComponent>(handle);
            if (!mesh.Enabled || scene.Registry().all_of<LODHiddenTag>(handle))
                continue;
            MeshPrimitive primitive;
            uint32_t meshHandle = 0;
            const AssetRef* meshRef = nullptr;
            if (!ResolveRendererMesh(renderer, scene, handle, primitive, meshHandle, &meshRef))
                continue; // Mesh Renderer without a (valid) Mesh Filter draws nothing
            const TransformComponent transform = scene.GetWorldTransform(entity);
            const float largestScale = std::max({ std::abs(transform.Scale.x), std::abs(transform.Scale.y), std::abs(transform.Scale.z) });
            const float radius = MeshReference::BoundingRadius(*meshRef) * largestScale;
            if (options.FrustumCull && !IsSphereVisibleInView(view, transform.Translation, radius)) {
                stats.CulledMeshes++;
                continue;
            }
            stats.VisibleMeshes++;
            const uint32_t subMeshCount = renderer.GetSubMeshCount(meshHandle);
            for (uint32_t i = 0; i < subMeshCount; i++) {
                Material material = ResolveMeshMaterial(MaterialForSubMesh(mesh.Materials, i));
                // Script-side material animation is a per-renderer property override, not a
                // mutation of the shared MaterialLoader cache. It deliberately replaces only
                // the material Color property; texture, shader and render state stay authored.
                if (mesh.HasRuntimeMaterialColor)
                    material.Color = mesh.RuntimeMaterialColor;
                MeshDrawItem item;
                MeshDrawCommand& command = item.Command;
                command.Primitive = primitive;
                command.MeshHandle = meshHandle;
                command.SubMeshIndex = i;
                command.Translation = transform.Translation;
                command.RotationDegrees = transform.Rotation;
                command.Scale = transform.Scale;
                command.Color = material.Color;
                command.ShadingMode = material.ShadingMode;
                command.HasMaterial = true;
                command.Material = BuildRenderMaterial(renderer, material, command.TextureId);
                finishCommand(item, transform.Translation, radius);
                item.Queue = material.EffectiveRenderQueue();
                items.push_back(item);
            }
        }

        auto skinnedView = scene.Registry().view<TransformComponent, SkinnedMeshRendererComponent>();
        for (auto handle : skinnedView) {
            Entity entity(handle, &scene);
            if (!ShouldRenderOnScreen(scene, handle, screen, options.CameraFilter) || !scene.IsEffectivelyActive(entity)) continue;
            auto& skin = skinnedView.get<SkinnedMeshRendererComponent>(handle);
            if (!skin.Enabled || skin.Mesh.Guid.empty() || scene.Registry().all_of<LODHiddenTag>(handle)) continue;
            const std::string path = AssetDatabase::ResolvePath(skin.Mesh.Guid);
            if (path.empty()) continue;
            const MeshData& source = MeshLoader::Load(path);
            if (source.Vertices.empty()) continue;
            RefreshSkinPose(scene, entity, skin, source);
            const TransformComponent transform = scene.GetWorldTransform(entity);
            const float largestScale = std::max({ std::abs(transform.Scale.x), std::abs(transform.Scale.y), std::abs(transform.Scale.z) });
            // Animated limbs can leave the bind-pose sphere; pad it so culling never pops them.
            const float radius = std::max(source.BoundingRadius, 0.5f) * largestScale * 1.5f;
            if (options.FrustumCull && !IsSphereVisibleInView(view, transform.Translation, radius)) {
                stats.CulledMeshes++;
                continue;
            }
            stats.VisibleMeshes++;
            const uint32_t meshHandle = ResolveMeshGeometry(renderer, skin.Mesh);
            const uint32_t subMeshCount = renderer.GetSubMeshCount(meshHandle);
            for (uint32_t i = 0; i < subMeshCount; ++i) {
                Material material = ResolveMeshMaterial(MaterialForSubMesh(skin.Materials, i));
                MeshDrawItem item;
                MeshDrawCommand& command = item.Command;
                command.Primitive = MeshPrimitive::Cube;
                command.MeshHandle = meshHandle;
                command.SubMeshIndex = i;
                command.Translation = transform.Translation;
                command.RotationDegrees = transform.Rotation;
                command.Scale = transform.Scale;
                command.Color = material.Color;
                command.ShadingMode = material.ShadingMode;
                command.HasMaterial = true;
                command.Material = BuildRenderMaterial(renderer, material, command.TextureId);

                const MeshData::SubMesh* subMesh = i < source.SubMeshes.size() ? &source.SubMeshes[i] : nullptr;
                if (subMesh && !subMesh->BonePalette.empty()) {
                    std::vector<glm::mat4>& palette = palettes.emplace_back();
                    palette.reserve(subMesh->BonePalette.size());
                    for (uint16_t bone : subMesh->BonePalette)
                        palette.push_back(bone < skin.RuntimeSkinMatrices.size() ? skin.RuntimeSkinMatrices[bone] : glm::mat4(1.0f));
                    command.SkinMatrices = palette.data();
                    command.SkinMatrixCount = static_cast<uint32_t>(palette.size());
                } else if (!source.Bones.empty()) {
                    // V2 assets predate per-draw palettes. Keep them functional, but update the
                    // CPU-deformed buffer only when their pose changes -- never once per screen.
                    if (skin.RuntimeMeshPath != path || skin.RuntimeMeshHandle == 0 || skin.RuntimeDynamicPoseSignature != skin.RuntimePoseSignature) {
                        MeshData deformed = SkinMesh(source, skin.RuntimeSkinMatrices);
                        if (skin.RuntimeMeshPath != path || skin.RuntimeMeshHandle == 0)
                            skin.RuntimeMeshHandle = renderer.CreateDynamicMesh(deformed);
                        else
                            renderer.UpdateDynamicMesh(skin.RuntimeMeshHandle, deformed);
                        skin.RuntimeMeshPath = path;
                        skin.RuntimeDynamicPoseSignature = skin.RuntimePoseSignature;
                    }
                    if (skin.RuntimeMeshHandle == 0) continue;
                    command.MeshHandle = skin.RuntimeMeshHandle;
                }
                // (A model placed on a SkinnedMeshRenderer before it has a rig draws through the
                // normal static path with the shared mesh handle.)
                finishCommand(item, transform.Translation, radius);
                item.Queue = material.EffectiveRenderQueue();
                items.push_back(item);
            }
        }

        // Unity ordering: render queue first. Opaque/cutout (queue <= 2500) are grouped by GPU
        // state then drawn front-to-back (early depth rejection); transparent/additive are drawn
        // back-to-front so blending composites correctly.
        std::stable_sort(items.begin(), items.end(), [](const MeshDrawItem& a, const MeshDrawItem& b) {
            if (a.Queue != b.Queue)
                return a.Queue < b.Queue;
            if (a.Queue <= 2500) {
                if (a.StateKey != b.StateKey)
                    return a.StateKey < b.StateKey;
                return a.Depth < b.Depth;
            }
            return a.Depth > b.Depth;
        });

        bool shadowsDrawn = false;
        const auto drawBlobShadows = [&]() {
            if (shadowsDrawn || !options.BlobShadows)
                return;
            shadowsDrawn = true;
            stats.BlobShadowDrawCalls = RenderDirectionalBlobShadows(renderer, scene, screen, view, options.CameraFilter);
            stats.MeshDrawCalls += stats.BlobShadowDrawCalls;
        };

        for (const MeshDrawItem& item : items) {
            // Projected blob shadows are translucent overlays on opaque receivers: after the
            // opaque queues, before anything transparent that should cover them.
            if (item.Queue > 2500)
                drawBlobShadows();
            renderer.DrawMesh(item.Command);
            stats.MeshDrawCalls++;
            const RenderMaterial& material = item.Command.Material;
            if (material.Model == LightingModel::VertexLit && view.MainLight.Enabled)
                stats.VertexLitDrawCalls++;
            if (UsesPerPixelLights(material.Model)) {
                stats.LitDrawCalls++;
                stats.LightsPerDrawTotal += item.Command.LightCount;
                stats.MaxLightsInDraw = std::max<uint32_t>(stats.MaxLightsInDraw, item.Command.LightCount);
            }
            if (material.Surface == SurfaceType::Transparent || material.Surface == SurfaceType::Additive)
                stats.TransparentDrawCalls++;
        }
        drawBlobShadows();
        return stats;
    }

    SceneRenderStats RenderScreen3D(IRenderer3D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor, bool clear) {
        SceneRenderStats stats;
        Entity camera = scene.GetPrimaryCamera(screen);
        if (!camera)
            return stats;

        const auto& cameraComponent = camera.GetComponent<CameraComponent>();
        RenderView renderView = BuildRenderView(scene, camera, screen);
        const uint32_t shadowCasters = RenderDirectionalShadowMap(renderer, scene, screen, renderView, cameraComponent);
        // Citro3D intentionally declines the optional shadow target today. Its
        // current result stays useful and safe by falling back to blob shadows.
        if (renderView.ShadowTechnique == ShadowMode::ShadowMapsExperimental && !renderView.HasShadowMap)
            renderView.ShadowTechnique = ShadowMode::BlobShadows;
        renderer.BeginScene(renderView, clearColor, clear);

        MeshPassOptions options;
        options.CameraFilter = &cameraComponent;
        stats = RenderMeshPass(renderer, scene, screen, renderView, options);
        stats.ShadowMapCasterDrawCalls = shadowCasters;

        renderer.EndScene();
        return stats;
    }

}
