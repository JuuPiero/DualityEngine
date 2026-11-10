#pragma once

// 2D world rendering components: Sprite Renderer, Tilemap, Line / Trail Renderer, Text Mesh and
// the CPU Particle System (drawn by SceneRenderer's 2D pass). Part of Scene/Components.h.

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/TextAlignment.h"

namespace Duality {

    // A quad of Size world units centered on the entity's
    // TransformComponent::Translation. Its final world size is Size multiplied
    // by TransformComponent::Scale.xy, matching Unity's SpriteRenderer / Transform
    // relationship. Texture is an AssetRef (empty Guid =
    // no texture assigned) -- when unresolved/absent, renders as a flat
    // Color-filled rect; when it resolves to an image, Color still
    // modulates it (white = unmodified). If the entity also has a
    // SpriteFlipbookComponent, that component's current frame overrides
    // this field entirely while it has a non-empty frame assigned.
    // Texture may also name one sprite of a sliced texture ("<textureGuid>#<spriteId>", see
    // Asset/SpriteDatabase.h): the quad then shows that sub-rect, and the sprite's own pivot
    // (Sprite Editor) replaces Pivot below, like Unity where the pivot belongs to the Sprite.
    // Unity's SpriteRenderer Draw Mode: Sliced draws a 9-slice using SliceBorder when it is
    // non-zero, else the sprite's border from the Sprite Editor. A non-zero SliceBorder always
    // slices (that is how scenes authored before Draw Mode existed enabled it).
    enum class SpriteDrawMode { Simple = 0, Sliced = 1 };

    struct SpriteRendererComponent {
        bool Enabled = true;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec2 Size{ 0.32f, 0.32f };
        AssetRef Texture;
        int SortOrder = 0;
        bool FlipX = false;
        bool FlipY = false;
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec4 SliceBorder{ 0.0f, 0.0f, 0.0f, 0.0f }; // left, right, top, bottom source px for 9-slice (0 = disabled)
        SpriteDrawMode DrawMode = SpriteDrawMode::Simple;
    };

    // Grid tilemap -- tile indices stored in external .tilemap asset.
    struct TilemapComponent {
        bool Enabled = true;
        int GridWidth = 16;
        int GridHeight = 16;
        glm::vec2 CellSize{ 0.16f, 0.16f };
        AssetRef Tileset;
        AssetRef TileData;
        int SortOrder = 0;
    };

    struct LineRendererComponent {
        // Fixed eight-point polyline: bounded memory and at most eight quad draws are a
        // deliberate 3DS budget, while still covering trails, ropes, aiming arcs and paths.
        bool Enabled = true;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float Width = 0.02f;
        bool Loop = false;
        // False matches Unity's default: points follow the entity (including parent transform).
        // True treats Point0..Point7 as world coordinates, useful for trajectories/pathfinding.
        bool UseWorldSpace = false;
        int PointCount = 2;
        glm::vec3 Point0{ 0.0f, 0.0f, 0.0f };
        glm::vec3 Point1{ 0.16f, 0.0f, 0.0f };
        glm::vec3 Point2{}, Point3{}, Point4{}, Point5{}, Point6{}, Point7{};
    };

    inline glm::vec3& GetLinePoint(LineRendererComponent& line, int index) {
        switch (index) {
            case 0: return line.Point0;
            case 1: return line.Point1;
            case 2: return line.Point2;
            case 3: return line.Point3;
            case 4: return line.Point4;
            case 5: return line.Point5;
            case 6: return line.Point6;
            default: return line.Point7;
        }
    }

    inline const glm::vec3& GetLinePoint(const LineRendererComponent& line, int index) {
        switch (index) {
            case 0: return line.Point0;
            case 1: return line.Point1;
            case 2: return line.Point2;
            case 3: return line.Point3;
            case 4: return line.Point4;
            case 5: return line.Point5;
            case 6: return line.Point6;
            default: return line.Point7;
        }
    }

    // Ribbon behind a moving entity (2D world XY, like Line Renderer). Bounded ring buffer: no
    // allocation per frame, at most MaxTrailPoints quads.
    inline constexpr int MaxTrailPoints = 64;
    struct TrailRendererComponent {
        bool Enabled = true;
        float Time = 1.0f;
        float MinVertexDistance = 0.02f;
        float StartWidth = 0.04f;
        float EndWidth = 0.0f;
        glm::vec4 StartColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 EndColor{ 1.0f, 1.0f, 1.0f, 0.0f };
        bool Emitting = true;
        bool Autodestruct = false;
        int SortOrder = 0;

        // Runtime ring buffer (oldest at RuntimeHead, RuntimeCount entries).
        glm::vec2 RuntimePoints[MaxTrailPoints]{};
        float RuntimeTimes[MaxTrailPoints]{};
        int RuntimeHead = 0;
        int RuntimeCount = 0;
        float RuntimeClock = 0.0f;
    };

    enum class TextAnchor { UpperLeft, UpperCenter, UpperRight, MiddleLeft, MiddleCenter, MiddleRight, LowerLeft, LowerCenter, LowerRight };

    // World-space text. On screen its line height is Font Size x Character Size / 100 world
    // units (so Font Size 16, Character Size 1 = 16 px at Zoom 1 with the default 100 PPU).
    struct TextMeshComponent {
        bool Enabled = true;
        std::string Text = "Hello World";
        float CharacterSize = 1.0f;
        float FontSize = 16.0f;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        TextAnchor Anchor = TextAnchor::MiddleCenter;
        TextAlignment Alignment = TextAlignment::Left;
        AssetRef Font;
        int SortOrder = 0;
    };

    // CPU particle emitter -- devkitPro-style simple burst/continuous particles.
    struct ParticleSystemComponent {
        bool Enabled = true;
        AssetRef Texture;
        bool Playing = true;
        bool Loop = true;
        float EmissionRate = 20.0f;
        int MaxParticles = 64;
        float Lifetime = 1.5f;
        float StartSpeed = 0.4f;
        float StartSize = 0.08f;
        glm::vec4 StartColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 EndColor{ 1.0f, 1.0f, 1.0f, 0.0f };
        glm::vec2 Gravity{ 0.0f, 0.6f };
        glm::vec2 VelocitySpread{ 0.3f, 0.3f };
        int SortOrder = 0;

        float EmissionAccumulator = 0.0f;
        int AliveCount = 0;
    };

    struct Particle {
        glm::vec2 Position;
        glm::vec2 Velocity;
        float Life = 0.0f;
        float MaxLife = 1.0f;
        float Size = 8.0f;
        glm::vec4 Color{ 1.0f };
    };

    inline constexpr int MaxParticlePoolSize = 256;

}
