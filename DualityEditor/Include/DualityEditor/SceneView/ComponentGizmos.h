#pragma once

#include <functional>

#include <imgui.h>
#include <glm/glm.hpp>

#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    struct EditorContext;

    // Scene-view gizmos/handles of the joint / effector / controller / LOD components of the selected
    // entity: Edge Collider 2D points (draggable), 2D joint anchors / axes / limits (anchors
    // draggable), Target Joint 2D target, effector shapes and directions (2D pane); Character
    // Controller capsule, 3D joint anchors / axes, Mesh Collider bounds and LOD Group state (3D
    // pane). Called by ScenePanel right after its own collider gizmos.
    void DrawComponentGizmos2D(EditorContext& ctx, Screen screen, ImDrawList* drawList,
                              const std::function<ImVec2(const glm::vec2&)>& worldToScreen, float worldToPixels);

    void DrawComponentGizmos3D(EditorContext& ctx, Screen screen, ImDrawList* drawList,
                              const std::function<bool(const glm::vec3&, ImVec2&)>& project);

}
