#pragma once

// Trail Renderer and Text Mesh (2D world pass) and LOD Group selection (3D mesh pass).

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    class IRenderer2D;
    class Scene;

    // Trail Renderers and Text Meshes of `screen`. A world point p is drawn at
    // (p.xy - cameraOrigin) * scale + screenOffset (the Game view passes the camera position,
    // world-to-pixel scale and screen center; the editor Scene view passes 0 / 1 / 0 because its
    // 2D projection is already in world units).
    void RenderWorldExtras2D(IRenderer2D& renderer, Scene& scene, Screen screen, const CameraComponent* camera,
                             const glm::vec2& cameraOrigin, float scale, const glm::vec2& screenOffset);

    // Picks every LOD Group's level for a camera at `cameraPosition` and tags the renderers of
    // the hidden levels with LODHiddenTag (the mesh passes skip those). Cheap: retags only when
    // a group's level changes.
    void ApplyLODGroups(Scene& scene, const glm::vec3& cameraPosition);

}
