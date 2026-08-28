#include "DualityEngine/Renderer/DrawHelpers2D.h"

#include <cmath>

#include "DualityEngine/Renderer/IRenderer2D.h"

namespace Duality {

    void DrawSpriteQuad(IRenderer2D& renderer, const glm::vec2& topLeft, const glm::vec2& size,
        const glm::vec4& color, float rotationDegrees, uint32_t textureId, bool flipX, bool flipY,
        const glm::vec4& uvRect) {
        glm::vec2 drawSize = size;
        if (flipX)
            drawSize.x = -drawSize.x;
        if (flipY)
            drawSize.y = -drawSize.y;
        renderer.DrawQuad(topLeft, drawSize, color, rotationDegrees, textureId, uvRect);
    }

    void DrawNineSlice(IRenderer2D& renderer, const glm::vec2& topLeft, const glm::vec2& size,
        const glm::vec4& color, uint32_t textureId, const glm::vec4& border) {
        float bl = border.x, br = border.y, bt = border.z, bb = border.w;
        if (bl <= 0.0f && br <= 0.0f && bt <= 0.0f && bb <= 0.0f) {
            renderer.DrawQuad(topLeft, size, color, 0.0f, textureId);
            return;
        }
        float w = size.x, h = size.y;
        float cx = w - bl - br;
        float cy = h - bt - bb;
        if (cx < 0.0f) cx = 0.0f;
        if (cy < 0.0f) cy = 0.0f;

        auto drawPatch = [&](float x, float y, float pw, float ph, glm::vec4 uv) {
            if (pw <= 0.0f || ph <= 0.0f)
                return;
            renderer.DrawQuad({ topLeft.x + x, topLeft.y + y }, { pw, ph }, color, 0.0f, textureId, uv);
        };

        drawPatch(0.0f, 0.0f, bl, bt, { 0.0f, 0.0f, 0.25f, 0.25f });
        drawPatch(bl, 0.0f, cx, bt, { 0.25f, 0.0f, 0.75f, 0.25f });
        drawPatch(bl + cx, 0.0f, br, bt, { 0.75f, 0.0f, 1.0f, 0.25f });
        drawPatch(0.0f, bt, bl, cy, { 0.0f, 0.25f, 0.25f, 0.75f });
        drawPatch(bl, bt, cx, cy, { 0.25f, 0.25f, 0.75f, 0.75f });
        drawPatch(bl + cx, bt, br, cy, { 0.75f, 0.25f, 1.0f, 0.75f });
        drawPatch(0.0f, bt + cy, bl, bb, { 0.0f, 0.75f, 0.25f, 1.0f });
        drawPatch(bl, bt + cy, cx, bb, { 0.25f, 0.75f, 0.75f, 1.0f });
        drawPatch(bl + cx, bt + cy, br, bb, { 0.75f, 0.75f, 1.0f, 1.0f });
    }

    void DrawLineStrip2D(IRenderer2D& renderer, const glm::vec2* points, int count, float width, const glm::vec4& color, bool loop) {
        if (count < 2)
            return;
        int segments = loop ? count : count - 1;
        for (int i = 0; i < segments; i++) {
            glm::vec2 a = points[i];
            glm::vec2 b = points[(i + 1) % count];
            glm::vec2 delta = b - a;
            float len = glm::length(delta);
            if (len < 0.001f)
                continue;
            glm::vec2 perp{ -delta.y / len * width * 0.5f, delta.x / len * width * 0.5f };
            glm::vec2 center = (a + b) * 0.5f;
            float angle = glm::degrees(std::atan2(delta.y, delta.x));
            renderer.DrawQuad({ center.x - len * 0.5f, center.y - width * 0.5f }, { len, width }, color, angle);
        }
    }

}
