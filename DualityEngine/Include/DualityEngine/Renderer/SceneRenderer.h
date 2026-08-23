#pragma once

#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Renderer/IRenderer3D.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // Shared per-screen render pass, used identically by DualityPlayer (real
    // 3DS screens) and DualityEditor's Game view (offscreen framebuffers) --
    // the only difference between platforms is which IRenderer2D/IRenderer3D
    // backend is passed in.
    //
    // Checks `screen`'s primary camera's Projection before doing anything
    // else: Perspective dispatches to RenderScreen3D (below) and returns --
    // Orthographic falls through to the original sprite-drawing body,
    // unchanged. A screen renders through exactly one pipeline per frame,
    // never both composited together (see IRenderer3D.h/CameraComponent::
    // Projection).
    //
    // Draws every (Transform, SpriteRenderer) entity in the scene, positioned
    // relative to `screen`'s primary CameraComponent entity (world position
    // and Zoom) -- cameras are their own entities, not tied to any one
    // sprite, matching Unity/Cocos convention. This is exactly what the
    // Editor's Game view and the real device show; the Editor's separate
    // Scene view (a free-roam editor-only camera, see OpenGLRenderer2D::
    // BeginCustomView) intentionally does not go through this function.
    void RenderScreen(IRenderer2D& renderer2D, IRenderer3D& renderer3D, Scene& scene, Screen screen, const glm::vec4& clearColor);

    // The Perspective half of RenderScreen's dispatch -- draws every
    // (Transform, MeshRenderer) entity in the scene through `screen`'s
    // primary camera (world position/rotation, Fov/Near/FarPlane), unlit.
    // Exposed separately (not just a RenderScreen implementation detail) so
    // the Editor's 3D Scene view pane can call it too, same convention as
    // GetActiveSpriteTexture/ResolveSpriteTexture below being shared with
    // the 2D Scene view. No-ops if `screen` has no primary camera, matching
    // RenderScreen's own no-camera behavior.
    void RenderScreen3D(IRenderer3D& renderer, Scene& scene, Screen screen, const glm::vec4& clearColor);

    // Whichever AssetRef should currently be drawn for this entity's
    // sprite: its SpriteFlipbookComponent's current frame if it has one
    // assigned (non-empty Guid), else its SpriteRendererComponent::
    // Texture directly. Shared between RenderScreen (above) and the
    // Editor's Scene view, which draws sprites directly rather than
    // through RenderScreen (no CameraComponent involved there).
    AssetRef GetActiveSpriteTexture(Scene& scene, entt::entity handle);

    // Resolves `textureRef` to a loaded GPU texture handle via
    // AssetDatabase + IRenderer2D::LoadTexture, or 0 if the ref is empty or
    // doesn't resolve to an existing file (falls back to DrawQuad's flat
    // Color path either way). Shared by RenderScreen and the Editor's
    // Scene view.
    uint32_t ResolveSpriteTexture(IRenderer2D& renderer, const AssetRef& textureRef);

    // Same guid->path->LoadTexture chain as ResolveSpriteTexture, just against
    // IRenderer3D's own LoadTexture instead. Shared by RenderScreen3D and the
    // Editor's 3D Scene view.
    uint32_t ResolveMeshTexture(IRenderer3D& renderer, const AssetRef& textureRef);

    // Guid->path->MaterialLoader::Load chain for a MeshRendererComponent::Material reference --
    // returns a default (white, no texture) Material when the ref is empty or doesn't resolve
    // to an existing file, same graceful-degradation convention as ResolveSpriteTexture/
    // ResolveMeshTexture. Shared by RenderScreen3D and the Editor's 3D Scene view.
    Material ResolveMeshMaterial(const AssetRef& materialRef);

    // Guid->path->IRenderer3D::LoadMesh chain for a MeshRendererComponent::Mesh reference --
    // returns 0 (meaning "fall back to the procedural MeshPrimitive") when the ref is empty or
    // doesn't resolve to an existing/parsable file. Shared by RenderScreen3D and the Editor's
    // 3D Scene view.
    uint32_t ResolveMeshGeometry(IRenderer3D& renderer, const AssetRef& meshRef);

    // True "2 worlds" screen separation: an entity tagged (directly or via an
    // ancestor) with ScreenGroupComponent/CameraComponent for the OTHER screen is
    // excluded entirely, regardless of where it sits relative to `screen`'s camera.
    // An untagged (Ungrouped) entity is always a candidate -- its actual
    // visibility still falls out of the existing position-relative-to-camera math,
    // exactly as before this feature existed (legacy/opt-out content keeps
    // working unchanged). Shared by RenderScreen and the Editor's split Scene view.
    bool ShouldRenderOnScreen(Scene& scene, entt::entity handle, Screen screen);

}
