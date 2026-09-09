#include "DualityEngine/Renderer/SceneRenderer.h"

#include <algorithm>
#include <vector>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Renderer/DrawHelpers2D.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Layer.h"
#include "DualityEngine/Scene/SceneRuntimeSystems.h"

namespace Duality {

    namespace {
        struct SpriteDrawItem {
            entt::entity Handle;
            int SortOrder = 0;
            float SortY = 0.0f;
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

    void RenderScreen(IRenderer2D& renderer2D, IRenderer3D& renderer3D, Scene& scene, Screen screen, const glm::vec4& clearColor) {
        Entity camera = scene.GetPrimaryCamera(screen);

        glm::vec4 effectiveClearColor = camera ? camera.GetComponent<CameraComponent>().Background : clearColor;

        if (camera)
            RenderScreen3D(renderer3D, scene, screen, effectiveClearColor, true);

        renderer2D.BeginScene(screen, effectiveClearColor, !camera);

        if (camera) {
            TransformComponent cameraTransform = scene.GetWorldTransform(camera);
            auto& cameraComponent = camera.GetComponent<CameraComponent>();
            float screenWidth, screenHeight;
            ScreenExtents(screen, screenWidth, screenHeight);

            std::vector<SpriteDrawItem> sprites;
            auto view = scene.Registry().view<TransformComponent, SpriteRendererComponent>();
            for (auto handle : view) {
                if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                    continue;
                if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                    continue;
                if (!view.get<SpriteRendererComponent>(handle).Enabled)
                    continue;
                TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));
                sprites.push_back({ handle, view.get<SpriteRendererComponent>(handle).SortOrder, transform.Translation.y });
            }
            std::sort(sprites.begin(), sprites.end(), [](const SpriteDrawItem& a, const SpriteDrawItem& b) {
                if (a.SortOrder != b.SortOrder)
                    return a.SortOrder < b.SortOrder;
                return a.SortY < b.SortY;
            });

            for (auto& item : sprites) {
                TransformComponent transform = scene.GetWorldTransform(Entity(item.Handle, &scene));
                auto& sprite = scene.Registry().get<SpriteRendererComponent>(item.Handle);

                glm::vec2 size = sprite.Size * cameraComponent.Zoom;
                glm::vec2 pivotOffset{ size.x * sprite.Pivot.x, size.y * sprite.Pivot.y };
                glm::vec2 screenCenter{
                    (transform.Translation.x - cameraTransform.Translation.x) * cameraComponent.Zoom + screenWidth * 0.5f,
                    (transform.Translation.y - cameraTransform.Translation.y) * cameraComponent.Zoom + screenHeight * 0.5f
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
                            (worldPos.x - cameraTransform.Translation.x) * cameraComponent.Zoom + screenWidth * 0.5f,
                            (worldPos.y - cameraTransform.Translation.y) * cameraComponent.Zoom + screenHeight * 0.5f
                        };
                        glm::vec2 size = tilemap.CellSize * cameraComponent.Zoom;
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
                int count = std::min(line.PointCount, 8);
                for (int i = 0; i < count; i++) {
                    glm::vec3 p = GetLinePoint(line, i) + transform.Translation;
                    points[i] = {
                        (p.x - cameraTransform.Translation.x) * cameraComponent.Zoom + screenWidth * 0.5f,
                        (p.y - cameraTransform.Translation.y) * cameraComponent.Zoom + screenHeight * 0.5f
                    };
                }
                DrawLineStrip2D(renderer2D, points, count, line.Width * cameraComponent.Zoom, line.Color, line.Loop);
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
                        (p.Position.x - cameraTransform.Translation.x) * cameraComponent.Zoom + screenWidth * 0.5f - p.Size * 0.5f,
                        (p.Position.y - cameraTransform.Translation.y) * cameraComponent.Zoom + screenHeight * 0.5f - p.Size * 0.5f
                    };
                    renderer2D.DrawQuad(topLeft, { p.Size, p.Size }, p.Color, 0.0f, tex);
                }
            }
        }

        RenderScreenUI(renderer2D, scene, screen);

        renderer2D.EndScene();
    }

    void RenderScreen3D(IRenderer3D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor, bool clear) {
        Entity camera = scene.GetPrimaryCamera(screen);
        if (!camera)
            return;

        TransformComponent cameraTransform = scene.GetWorldTransform(camera);
        auto& cameraComponent = camera.GetComponent<CameraComponent>();
        float screenWidth, screenHeight;
        ScreenExtents(screen, screenWidth, screenHeight);

        float orthoHalfHeight = screenHeight * 0.5f / cameraComponent.Zoom;

        renderer.BeginScene(screen, cameraComponent.Projection, cameraTransform.Translation, cameraTransform.Rotation, cameraComponent.FovDegrees, orthoHalfHeight, screenWidth / screenHeight, cameraComponent.NearPlane, cameraComponent.FarPlane, clearColor, clear);

        auto view = scene.Registry().view<TransformComponent, MeshRendererComponent>();
        for (auto handle : view) {
            if (!ShouldRenderOnScreen(scene, handle, screen, &cameraComponent))
                continue;
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& mesh = view.get<MeshRendererComponent>(handle);
            if (!mesh.Enabled)
                continue;
            TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));
            uint32_t meshHandle = ResolveMeshGeometry(renderer, mesh.Mesh);

            uint32_t subMeshCount = renderer.GetSubMeshCount(meshHandle);
            for (uint32_t i = 0; i < subMeshCount; i++) {
                const AssetRef& materialRef = MaterialForSubMesh(mesh.Materials, i);
                Material material = ResolveMeshMaterial(materialRef);
                uint32_t textureId = ResolveMeshTexture(renderer, material.Texture);
                renderer.DrawMesh(mesh.Primitive, meshHandle, i, transform.Translation, transform.Rotation, transform.Scale, material.Color, textureId);
            }
        }

        renderer.EndScene();
    }

}
