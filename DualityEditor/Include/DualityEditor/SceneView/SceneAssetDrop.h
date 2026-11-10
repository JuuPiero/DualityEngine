#pragma once

#include <imgui.h>

#include <glm/glm.hpp>

#include "DualityEngine/Renderer/Screen.h"

namespace Duality {

    struct EditorContext;

    // Scene view drop target for Project-window assets (ASSET_GUID payload): images create a
    // Sprite (or a UI Image under the selected Canvas), prefabs / models a prefab instance,
    // meshes a Mesh Filter + Mesh Renderer, materials a built-in Cube using it, audio an Audio
    // Source, animation clips an Animation. Call right after the pane's framebuffer Image(),
    // while it is ImGui's active drop target; `worldPosition` / `canvasPosition` are the drop
    // point in world space and in the selected Canvas' space.
    void AcceptSceneAssetDrop(EditorContext& ctx, Screen screen, ImVec2 imagePos, ImVec2 imageSize,
        const glm::vec3& worldPosition, const glm::vec2& canvasPosition);

}
