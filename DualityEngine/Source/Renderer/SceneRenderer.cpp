#include "DualityEngine/Renderer/SceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Physics/PhysicsUnits.h"
#include "DualityEngine/Renderer/DrawHelpers2D.h"
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

        void ResolveSpriteVisual(Scene& scene, entt::entity handle, AssetRef& outTexture, glm::vec4& outUV) {
            outUV = { 0.0f, 0.0f, 1.0f, 1.0f };
            if (scene.Registry().all_of<SpriteSheetAnimatorComponent>(handle)) {
                auto& anim = scene.Registry().get<SpriteSheetAnimatorComponent>(handle);
                if (anim.Enabled && !anim.Texture.Guid.empty()) {
                    outTexture = anim.Texture;
                    outUV = FrameUV(anim.CurrentFrame, anim.Columns, anim.Rows);
                    return;
                }
            }
            outTexture = GetActiveSpriteTexture(scene, handle);
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

        float PrimitiveShadowRadius(MeshPrimitive primitive) {
            switch (primitive) {
                case MeshPrimitive::Sphere:
                case MeshPrimitive::Capsule: return 0.5f;
                case MeshPrimitive::Cube:    return 0.5f * std::sqrt(3.0f);
                default:                      return 0.5f;
            }
        }

        float MeshBoundingRadius(const MeshRendererComponent& mesh) {
            if (!mesh.Mesh.Guid.empty()) {
                const std::string path = AssetDatabase::ResolvePath(mesh.Mesh.Guid);
                if (!path.empty()) {
                    const float importedRadius = MeshLoader::Load(path).BoundingRadius;
                    if (importedRadius > 0.0001f)
                        return importedRadius;
                }
            }
            return PrimitiveShadowRadius(mesh.Primitive);
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
        view.MainLight = {};
        view.ShadowTechnique = RenderSettings::GetEffectiveShadowMode();
        // Storage order is an EnTT implementation detail. Use canonical hierarchy order so the
        // Game renderer and the editor's free Scene camera make the same deterministic choice.
        for (Entity light : scene.GetHierarchyTraversalOrder()) {
            if (!light.HasComponent<DirectionalLightComponent>())
                continue;
            const DirectionalLightComponent& component = light.GetComponent<DirectionalLightComponent>();
            if (!component.Enabled || !scene.IsEffectivelyActive(light))
                continue;
            const TransformComponent transform = scene.GetWorldTransform(light);
            const glm::mat4 rotation = RotationMatrix(transform.Rotation);
            // Entity forward is -Z; lighting needs the direction from a point toward the light.
            view.MainLight.Enabled = true;
            view.MainLight.Direction = -glm::normalize(glm::vec3(rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            view.MainLight.Color = component.Color;
            view.MainLight.Intensity = component.Intensity;
            view.MainLight.CastShadows = component.CastShadows;
            break;
        }
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

    static bool IsMeshVisibleInCamera(Scene& scene, Entity entity, const MeshRendererComponent& mesh,
        const CameraComponent& camera, const TransformComponent& cameraTransform, Screen screen) {
        const TransformComponent transform = scene.GetWorldTransform(entity);
        const float largestScale = std::max({ std::abs(transform.Scale.x), std::abs(transform.Scale.y), std::abs(transform.Scale.z) });
        const float radius = MeshBoundingRadius(mesh) * largestScale;

        // Camera local space uses -Z forward. A sphere test is deliberately
        // conservative: false positives are cheap; a false negative would pop
        // visible geometry. It provides an actual culling win without requiring
        // per-mesh bounds buffers on a memory-limited 3DS.
        const glm::mat4 inverseCameraRotation = glm::inverse(RotationMatrix(cameraTransform.Rotation));
        const glm::vec3 localPosition = glm::vec3(inverseCameraRotation * glm::vec4(transform.Translation - cameraTransform.Translation, 0.0f));
        const float depth = -localPosition.z;
        if (depth + radius < camera.NearPlane || depth - radius > camera.FarPlane)
            return false;

        float screenWidth = 0.0f, screenHeight = 0.0f;
        ScreenExtents(screen, screenWidth, screenHeight);
        if (camera.Projection == ProjectionType::Perspective) {
            if (depth < -radius)
                return false;
            const float halfHeight = std::tan(glm::radians(camera.FovDegrees) * 0.5f) * std::max(depth, 0.0f);
            const float halfWidth = halfHeight * (screenWidth / screenHeight);
            return std::abs(localPosition.x) <= halfWidth + radius && std::abs(localPosition.y) <= halfHeight + radius;
        }

        const float halfHeight = screenHeight * 0.5f / camera.Zoom;
        const float halfWidth = halfHeight * (screenWidth / screenHeight);
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
            if (!mesh.Enabled || !scene.IsEffectivelyActive(entity))
                continue;
            const TransformComponent transform = scene.GetWorldTransform(entity);
            const uint32_t meshHandle = ResolveMeshGeometry(renderer, mesh.Mesh);
            const uint32_t subMeshCount = renderer.GetSubMeshCount(meshHandle);
            for (uint32_t i = 0; i < subMeshCount; ++i) {
                renderer.DrawDirectionalShadowCaster(MeshDrawCommand{ mesh.Primitive, meshHandle, i,
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

        if (camera)
            stats += RenderScreen3D(renderer3D, scene, screen, effectiveClearColor, clear);

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
            std::sort(sprites.begin(), sprites.end(), [](const SpriteDrawItem& a, const SpriteDrawItem& b) {
                if (a.SortOrder != b.SortOrder)
                    return a.SortOrder < b.SortOrder;
                return a.HierarchyOrder < b.HierarchyOrder;
            });

            for (auto& item : sprites) {
                TransformComponent transform = scene.GetWorldTransform(Entity(item.Handle, &scene));
                auto& sprite = scene.Registry().get<SpriteRendererComponent>(item.Handle);

                // SpriteRenderer::Size is the unscaled quad size. Transform.Scale
                // is deliberately applied here (and in both editor Scene panes),
                // so using the Scale gizmo changes the same object in Game and
                // Scene just like Unity.
                glm::vec2 size = sprite.Size * glm::vec2(transform.Scale.x, transform.Scale.y) * worldToPixels;
                glm::vec2 pivotOffset{ size.x * sprite.Pivot.x, size.y * sprite.Pivot.y };
                glm::vec2 screenCenter{
                    (transform.Translation.x - cameraTransform.Translation.x) * worldToPixels + screenWidth * 0.5f,
                    (transform.Translation.y - cameraTransform.Translation.y) * worldToPixels + screenHeight * 0.5f
                };
                glm::vec2 topLeft = screenCenter - pivotOffset;

                AssetRef texRef;
                glm::vec4 uv;
                ResolveSpriteVisual(scene, item.Handle, texRef, uv);
                uint32_t textureId = ResolveSpriteTexture(renderer2D, texRef);

                if (sprite.SliceBorder.x > 0.0f || sprite.SliceBorder.y > 0.0f || sprite.SliceBorder.z > 0.0f || sprite.SliceBorder.w > 0.0f)
                    DrawNineSlice(renderer2D, topLeft, size, sprite.Color, textureId, sprite.SliceBorder);
                else
                    DrawSpriteQuad(renderer2D, topLeft, size, sprite.Color, transform.Rotation.z, textureId, sprite.FlipX, sprite.FlipY, uv);
                stats.VisibleSprites++;
            }

            auto tileView = scene.Registry().view<TransformComponent, TilemapComponent>();
            for (auto handle : tileView) {
                if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                    continue;
                if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                    continue;
                auto& tilemap = tileView.get<TilemapComponent>(handle);
                if (!tilemap.Enabled)
                    continue;
                TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));
                TilemapData data;
                if (!tilemap.TileData.Guid.empty())
                    data = TilemapLoader::Load(AssetDatabase::ResolvePath(tilemap.TileData.Guid));
                uint32_t tileTex = ResolveSpriteTexture(renderer2D, tilemap.Tileset);
                int w = data.Width > 0 ? data.Width : tilemap.GridWidth;
                int h = data.Height > 0 ? data.Height : tilemap.GridHeight;
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        int idx = y * w + x;
                        if (idx >= static_cast<int>(data.Tiles.size()) || data.Tiles[idx] < 0)
                            continue;
                        glm::vec2 worldPos = {
                            transform.Translation.x + x * tilemap.CellSize.x,
                            transform.Translation.y + y * tilemap.CellSize.y
                        };
                        glm::vec2 screenCenter{
                            (worldPos.x - cameraTransform.Translation.x) * worldToPixels + screenWidth * 0.5f,
                            (worldPos.y - cameraTransform.Translation.y) * worldToPixels + screenHeight * 0.5f
                        };
                        glm::vec2 size = tilemap.CellSize * worldToPixels;
                        glm::vec2 topLeft = screenCenter - size * 0.5f;
                        renderer2D.DrawQuad(topLeft, size, { 1, 1, 1, 1 }, 0.0f, tileTex);
                    }
                }
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
                for (const Particle& p : GetParticlePool(handle)) {
                    glm::vec2 topLeft{
                        (p.Position.x - cameraTransform.Translation.x) * worldToPixels + screenWidth * 0.5f - p.Size * worldToPixels * 0.5f,
                        (p.Position.y - cameraTransform.Translation.y) * worldToPixels + screenHeight * 0.5f - p.Size * worldToPixels * 0.5f
                    };
                    renderer2D.DrawQuad(topLeft, { p.Size * worldToPixels, p.Size * worldToPixels }, p.Color, 0.0f, tex);
                }
            }
        }

        RenderScreenUI(renderer2D, scene, screen);

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
            if (!mesh.Enabled || mesh.Primitive != MeshPrimitive::Plane || !scene.IsEffectivelyActive(entity))
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
            if (!mesh.Enabled || mesh.Primitive == MeshPrimitive::Plane || !scene.IsEffectivelyActive(caster))
                continue;
            const TransformComponent casterTransform = scene.GetWorldTransform(caster);
            const float maxScale = std::max({ std::abs(casterTransform.Scale.x), std::abs(casterTransform.Scale.y), std::abs(casterTransform.Scale.z) });
            const float shadowDiameter = std::max(0.02f, PrimitiveShadowRadius(mesh.Primitive) * maxScale * 2.0f);

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

    SceneRenderStats RenderScreen3D(IRenderer3D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor, bool clear) {
        SceneRenderStats stats;
        Entity camera = scene.GetPrimaryCamera(screen);
        if (!camera)
            return stats;

        const auto& cameraComponent = camera.GetComponent<CameraComponent>();
        const TransformComponent cameraTransform = scene.GetWorldTransform(camera);
        RenderView renderView = BuildRenderView(scene, camera, screen);
        stats.ShadowMapCasterDrawCalls = RenderDirectionalShadowMap(renderer, scene, screen, renderView, cameraComponent);
        // Citro3D intentionally declines the optional shadow target today. Its
        // current result stays useful and safe by falling back to blob shadows.
        if (renderView.ShadowTechnique == ShadowMode::ShadowMapsExperimental && !renderView.HasShadowMap)
            renderView.ShadowTechnique = ShadowMode::BlobShadows;
        renderer.BeginScene(renderView, clearColor, clear);

        auto view = scene.Registry().view<TransformComponent, MeshRendererComponent>();
        for (auto handle : view) {
            if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                continue;
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& mesh = view.get<MeshRendererComponent>(handle);
            if (!mesh.Enabled)
                continue;
            Entity entity(handle, &scene);
            if (!IsMeshVisibleInCamera(scene, entity, mesh, cameraComponent, cameraTransform, screen)) {
                stats.CulledMeshes++;
                continue;
            }
            stats.VisibleMeshes++;
            TransformComponent transform = scene.GetWorldTransform(entity);
            uint32_t meshHandle = ResolveMeshGeometry(renderer, mesh.Mesh);

            uint32_t subMeshCount = renderer.GetSubMeshCount(meshHandle);
            for (uint32_t i = 0; i < subMeshCount; i++) {
                const AssetRef& materialRef = MaterialForSubMesh(mesh.Materials, i);
                Material material = ResolveMeshMaterial(materialRef);
                // Script-side material animation is a per-renderer property override, not a
                // mutation of the shared MaterialLoader cache. It deliberately replaces only
                // the material Color property; Texture and VertexLit/Unlit remain authored.
                if (mesh.HasRuntimeMaterialColor)
                    material.Color = mesh.RuntimeMaterialColor;
                uint32_t textureId = ResolveMeshTexture(renderer, material.Texture);
                renderer.DrawMesh(MeshDrawCommand{ mesh.Primitive, meshHandle, i, transform.Translation, transform.Rotation, transform.Scale, material.Color, textureId, material.ShadingMode });
                stats.MeshDrawCalls++;
                if (material.ShadingMode == MaterialShadingMode::VertexLit && renderView.MainLight.Enabled)
                    stats.VertexLitDrawCalls++;
            }
        }

        auto skinnedView = scene.Registry().view<TransformComponent, SkinnedMeshRendererComponent>();
        for (auto handle : skinnedView) {
            Entity entity(handle, &scene);
            if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent) || !scene.IsEffectivelyActive(entity)) continue;
            auto& skin = skinnedView.get<SkinnedMeshRendererComponent>(handle);
            if (!skin.Enabled || skin.Mesh.Guid.empty()) continue;
            const std::string path = AssetDatabase::ResolvePath(skin.Mesh.Guid);
            if (path.empty()) continue;
            const MeshData& source = MeshLoader::Load(path);
            if (source.Vertices.empty()) continue;
            RefreshSkinPose(scene, entity, skin, source);
            const TransformComponent transform = scene.GetWorldTransform(entity);
            const uint32_t meshHandle = ResolveMeshGeometry(renderer, skin.Mesh);
            const uint32_t subMeshCount = renderer.GetSubMeshCount(meshHandle);
            for (uint32_t i = 0; i < subMeshCount; ++i) {
                Material material = ResolveMeshMaterial(MaterialForSubMesh(skin.Materials, i));
                const MeshData::SubMesh* subMesh = i < source.SubMeshes.size() ? &source.SubMeshes[i] : nullptr;
                if (subMesh && !subMesh->BonePalette.empty()) {
                    skin.RuntimePaletteMatrices.clear();
                    skin.RuntimePaletteMatrices.reserve(subMesh->BonePalette.size());
                    for (uint16_t bone : subMesh->BonePalette)
                        skin.RuntimePaletteMatrices.push_back(bone < skin.RuntimeSkinMatrices.size() ? skin.RuntimeSkinMatrices[bone] : glm::mat4(1.0f));
                    renderer.DrawMesh(MeshDrawCommand{ MeshPrimitive::Cube, meshHandle, i, transform.Translation, transform.Rotation, transform.Scale,
                        material.Color, ResolveMeshTexture(renderer, material.Texture), material.ShadingMode, false, true,
                        skin.RuntimePaletteMatrices.data(), static_cast<uint32_t>(skin.RuntimePaletteMatrices.size()) });
                } else if (source.Bones.empty()) {
                    // A model may be placed on a SkinnedMeshRenderer before it has a rig; draw
                    // it through the normal static path instead of allocating a mutable copy.
                    renderer.DrawMesh(MeshDrawCommand{ MeshPrimitive::Cube, meshHandle, i, transform.Translation, transform.Rotation, transform.Scale,
                        material.Color, ResolveMeshTexture(renderer, material.Texture), material.ShadingMode });
                } else {
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
                    renderer.DrawMesh(MeshDrawCommand{ MeshPrimitive::Cube, skin.RuntimeMeshHandle, i, transform.Translation, transform.Rotation, transform.Scale,
                        material.Color, ResolveMeshTexture(renderer, material.Texture), material.ShadingMode });
                }
                stats.MeshDrawCalls++;
                if (material.ShadingMode == MaterialShadingMode::VertexLit && renderView.MainLight.Enabled)
                    stats.VertexLitDrawCalls++;
            }
        }

        stats.BlobShadowDrawCalls = RenderDirectionalBlobShadows(renderer, scene, screen, renderView, &cameraComponent);
        stats.MeshDrawCalls += stats.BlobShadowDrawCalls;

        renderer.EndScene();
        return stats;
    }

}
