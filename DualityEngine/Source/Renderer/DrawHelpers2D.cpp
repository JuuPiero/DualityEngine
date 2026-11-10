#include "DualityEngine/Renderer/DrawHelpers2D.h"

#include <algorithm>
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
        // A negative size mirrors the quad around its own center (DrawQuad's position is the
        // corner that becomes top-left before mirroring), so shift the origin to keep the
        // mirrored quad covering the same rect.
        glm::vec2 origin = topLeft;
        if (flipX)
            origin.x += size.x;
        if (flipY)
            origin.y += size.y;
        renderer.DrawQuad(origin, drawSize, color, rotationDegrees, textureId, uvRect);
    }

    glm::vec4 ComposeUV(const glm::vec4& outer, const glm::vec4& inner) {
        const float w = outer.z - outer.x, h = outer.w - outer.y;
        return { outer.x + inner.x * w, outer.y + inner.y * h, outer.x + inner.z * w, outer.y + inner.w * h };
    }

    namespace {
        // Scales a pair of border sizes down proportionally when they exceed `extent`.
        void FitBorders(float& a, float& b, float extent) {
            a = std::max(0.0f, a);
            b = std::max(0.0f, b);
            const float total = a + b;
            if (total > extent && total > 0.0f) {
                const float scale = extent / total;
                a *= scale;
                b *= scale;
            }
        }
    }

    int DrawSprite2D(IRenderer2D& renderer, const SpriteDrawData& sprite) {
        const glm::vec2 size = glm::abs(sprite.Size);
        if (size.x <= 0.0f || size.y <= 0.0f)
            return 0;
        const float sx = sprite.FlipX ? -1.0f : 1.0f;
        const float sy = sprite.FlipY ? -1.0f : 1.0f;
        const float radians = glm::radians(sprite.RotationDegrees);
        const float c = std::cos(radians), s = std::sin(radians);
        auto toDraw = [&](glm::vec2 local) {
            // local is relative to the pivot (already mirrored); rotate around the pivot.
            return sprite.PivotPosition + glm::vec2(local.x * c - local.y * s, local.x * s + local.y * c);
        };
        // Rect center relative to the pivot, before mirroring.
        const glm::vec2 rectCenter{ (0.5f - sprite.Pivot.x) * size.x, (0.5f - sprite.Pivot.y) * size.y };

        const bool sliced = sprite.Sliced && sprite.TextureId != 0 &&
            (sprite.BorderDraw.x > 0.0f || sprite.BorderDraw.y > 0.0f || sprite.BorderDraw.z > 0.0f || sprite.BorderDraw.w > 0.0f);
        if (!sliced) {
            const glm::vec2 signedSize{ size.x * sx, size.y * sy };
            const glm::vec2 center = toDraw({ rectCenter.x * sx, rectCenter.y * sy });
            renderer.DrawQuad(center - signedSize * 0.5f, signedSize, sprite.Color, sprite.RotationDegrees, sprite.TextureId, sprite.UV);
            return 1;
        }

        float bl = sprite.BorderDraw.x, br = sprite.BorderDraw.y, bt = sprite.BorderDraw.z, bb = sprite.BorderDraw.w;
        FitBorders(bl, br, size.x);
        FitBorders(bt, bb, size.y);
        const float xs[4] = { -size.x * 0.5f, -size.x * 0.5f + bl, size.x * 0.5f - br, size.x * 0.5f };
        const float ys[4] = { -size.y * 0.5f, -size.y * 0.5f + bt, size.y * 0.5f - bb, size.y * 0.5f };
        const glm::vec4& uv = sprite.UV;
        const float us[4] = { uv.x, uv.x + sprite.BorderUV.x, uv.z - sprite.BorderUV.y, uv.z };
        const float vs[4] = { uv.y, uv.y + sprite.BorderUV.z, uv.w - sprite.BorderUV.w, uv.w };
        int quads = 0;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const float w = xs[column + 1] - xs[column];
                const float h = ys[row + 1] - ys[row];
                if (w <= 0.0f || h <= 0.0f)
                    continue;
                const glm::vec2 patchCenter{ rectCenter.x + (xs[column] + xs[column + 1]) * 0.5f, rectCenter.y + (ys[row] + ys[row + 1]) * 0.5f };
                const glm::vec2 signedSize{ w * sx, h * sy };
                const glm::vec2 center = toDraw({ patchCenter.x * sx, patchCenter.y * sy });
                renderer.DrawQuad(center - signedSize * 0.5f, signedSize, sprite.Color, sprite.RotationDegrees, sprite.TextureId,
                    { us[column], vs[row], us[column + 1], vs[row + 1] });
                ++quads;
            }
        }
        return quads;
    }

    void DrawNineSlice(IRenderer2D& renderer, const glm::vec2& topLeft, const glm::vec2& size,
        const glm::vec4& color, uint32_t textureId, const glm::vec4& border, const glm::vec4& uvRect) {
        SpriteDrawData sprite;
        sprite.PivotPosition = topLeft;
        sprite.Pivot = { 0.0f, 0.0f };
        sprite.Size = size;
        sprite.Color = color;
        sprite.TextureId = textureId;
        sprite.UV = uvRect;
        sprite.Sliced = true;
        sprite.BorderDraw = border;
        const glm::vec2 textureSize = textureId != 0 ? renderer.GetTextureSize(textureId) : glm::vec2(0.0f);
        if (textureSize.x > 0.0f && textureSize.y > 0.0f)
            sprite.BorderUV = { border.x / textureSize.x, border.y / textureSize.x, border.z / textureSize.y, border.w / textureSize.y };
        else
            sprite.BorderUV = { 0.25f * (uvRect.z - uvRect.x), 0.25f * (uvRect.z - uvRect.x), 0.25f * (uvRect.w - uvRect.y), 0.25f * (uvRect.w - uvRect.y) };
        DrawSprite2D(renderer, sprite);
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
            glm::vec2 center = (a + b) * 0.5f;
            float angle = glm::degrees(std::atan2(delta.y, delta.x));
            renderer.DrawQuad({ center.x - len * 0.5f, center.y - width * 0.5f }, { len, width }, color, angle);
        }
    }

}
