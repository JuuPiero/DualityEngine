#include "DualityEngine/Renderer/SceneRenderer.h"

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    AssetRef GetActiveSpriteTexture(Scene& scene, entt::entity handle) {
        if (scene.Registry().all_of<SpriteFlipbookComponent>(handle)) {
            auto& flipbook = scene.Registry().get<SpriteFlipbookComponent>(handle);
            AssetRef& frame = GetFlipbookFrame(flipbook, flipbook.CurrentFrame);
            if (!frame.Guid.empty())
                return frame;
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

    bool ShouldRenderOnScreen(Scene& scene, entt::entity handle, Screen screen) {
        Screen entityScreen;
        if (scene.TryResolveEntityScreen(Entity(handle, &scene), entityScreen))
            return entityScreen == screen;
        return true; // Ungrouped -- still a candidate, position-relative-to-camera decides
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
        if (camera && camera.GetComponent<CameraComponent>().Projection == ProjectionType::Perspective) {
            RenderScreen3D(renderer3D, scene, screen, clearColor);
            return;
        }

        renderer2D.BeginScene(screen, clearColor);

        if (camera) {
            TransformComponent cameraTransform = scene.GetWorldTransform(camera);
            auto& cameraComponent = camera.GetComponent<CameraComponent>();
            float screenWidth, screenHeight;
            ScreenExtents(screen, screenWidth, screenHeight);

            // Every untagged sprite in the scene is a candidate for this screen --
            // whether it ends up visible depends purely on where it sits relative
            // to this camera (a sprite far from every active camera just draws off
            // the fixed 400x240/320x240 target, which is effectively culling by
            // construction). This is real camera-relative composition, not "only
            // the camera's own sprite" like the very first version of this
            // function. A sprite tagged (directly or via an ancestor) for the
            // OTHER screen is skipped outright -- true "2 worlds" separation, not
            // just position-based culling.
            auto view = scene.Registry().view<TransformComponent, SpriteRendererComponent>();
            for (auto handle : view) {
                if (!ShouldRenderOnScreen(scene, handle, screen))
                    continue;
                TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));
                auto& sprite = view.get<SpriteRendererComponent>(handle);

                glm::vec2 screenCenter{
                    (transform.Translation.x - cameraTransform.Translation.x) * cameraComponent.Zoom + screenWidth * 0.5f,
                    (transform.Translation.y - cameraTransform.Translation.y) * cameraComponent.Zoom + screenHeight * 0.5f
                };
                glm::vec2 size = sprite.Size * cameraComponent.Zoom;
                uint32_t textureId = ResolveSpriteTexture(renderer2D, GetActiveSpriteTexture(scene, handle));

                renderer2D.DrawQuad({ screenCenter.x - size.x * 0.5f, screenCenter.y - size.y * 0.5f }, size, sprite.Color, transform.Rotation.z, textureId);
            }
        }

        renderer2D.EndScene();
    }

    void RenderScreen3D(IRenderer3D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor) {
        Entity camera = scene.GetPrimaryCamera(screen);
        if (!camera)
            return; // nothing to render without a camera, matching RenderScreen's own no-camera behavior

        TransformComponent cameraTransform = scene.GetWorldTransform(camera);
        auto& cameraComponent = camera.GetComponent<CameraComponent>();
        float screenWidth, screenHeight;
        ScreenExtents(screen, screenWidth, screenHeight);

        renderer.BeginScene(screen, cameraTransform.Translation, cameraTransform.Rotation, cameraComponent.FovDegrees, screenWidth / screenHeight, cameraComponent.NearPlane, cameraComponent.FarPlane, clearColor);

        auto view = scene.Registry().view<TransformComponent, MeshRendererComponent>();
        for (auto handle : view) {
            if (!ShouldRenderOnScreen(scene, handle, screen))
                continue;
            TransformComponent transform = scene.GetWorldTransform(Entity(handle, &scene));
            auto& mesh = view.get<MeshRendererComponent>(handle);
            uint32_t textureId = ResolveMeshTexture(renderer, mesh.Texture);

            renderer.DrawMesh(mesh.Primitive, transform.Translation, transform.Rotation, transform.Scale, mesh.Color, textureId);
        }

        renderer.EndScene();
    }

}
