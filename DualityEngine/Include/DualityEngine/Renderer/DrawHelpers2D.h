#pragma once

#include <glm/glm.hpp>

namespace Duality {

    class IRenderer2D;
    class Scene;

    // Shared 2D draw helpers (9-slice, lines, sprite with flip/UV).
    void DrawSpriteQuad(IRenderer2D& renderer, const glm::vec2& topLeft, const glm::vec2& size,
        const glm::vec4& color, float rotationDegrees, uint32_t textureId, bool flipX, bool flipY,
        const glm::vec4& uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    void DrawNineSlice(IRenderer2D& renderer, const glm::vec2& topLeft, const glm::vec2& size,
        const glm::vec4& color, uint32_t textureId, const glm::vec4& border);

    void DrawLineStrip2D(IRenderer2D& renderer, const glm::vec2* points, int count, float width, const glm::vec4& color, bool loop);

}
