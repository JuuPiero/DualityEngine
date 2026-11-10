#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Duality {

    class IRenderer2D;
    class Scene;

    // Shared 2D draw helpers (9-slice, lines, sprite with flip/UV).
    void DrawSpriteQuad(IRenderer2D& renderer, const glm::vec2& topLeft, const glm::vec2& size,
        const glm::vec4& color, float rotationDegrees, uint32_t textureId, bool flipX, bool flipY,
        const glm::vec4& uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    // 9-slice of the whole texture / `uvRect`, axis-aligned. `border` = left, right, top,
    // bottom in source-texture pixels, drawn at 1 texture pixel = 1 draw unit (UI convention).
    // The UV split comes from the texture's real size (IRenderer2D::GetTextureSize), falling
    // back to quarters when the backend cannot report it.
    void DrawNineSlice(IRenderer2D& renderer, const glm::vec2& topLeft, const glm::vec2& size,
        const glm::vec4& color, uint32_t textureId, const glm::vec4& border,
        const glm::vec4& uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

    // Everything needed to draw one sprite (SpriteRenderer, UI Image, tile...). Positions and
    // sizes are in the target's draw space (screen pixels in the Game view, world units in
    // the editor Scene view).
    struct SpriteDrawData {
        glm::vec2 PivotPosition{ 0.0f, 0.0f }; // where the pivot lands
        glm::vec2 Size{ 0.0f, 0.0f };          // unsigned rect size
        glm::vec2 Pivot{ 0.5f, 0.5f };         // normalized, (0,0) = top-left
        float RotationDegrees = 0.0f;          // around the pivot
        bool FlipX = false;                    // mirrored around the pivot
        bool FlipY = false;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        uint32_t TextureId = 0;
        glm::vec4 UV{ 0.0f, 0.0f, 1.0f, 1.0f };
        bool Sliced = false;
        glm::vec4 BorderDraw{ 0.0f };          // left, right, top, bottom in draw units
        glm::vec4 BorderUV{ 0.0f };            // the same border as a fraction of the texture
    };

    // Draws a SpriteDrawData: one quad (Simple) or nine (Sliced). Returns the quads issued.
    int DrawSprite2D(IRenderer2D& renderer, const SpriteDrawData& sprite);

    // Composes a sub-rect (normalized within `outer`) into texture UV space -- e.g. a sprite-
    // sheet animator's grid cell inside a sliced sprite.
    glm::vec4 ComposeUV(const glm::vec4& outer, const glm::vec4& inner);

    void DrawLineStrip2D(IRenderer2D& renderer, const glm::vec2* points, int count, float width, const glm::vec4& color, bool loop);

}
