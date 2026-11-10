// Registration of Scene/Components/Rendering2D.h. Display and field names are serialized: treat
// them as a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterRendering2DComponents() {
        Component<SpriteRendererComponent>("Sprite Renderer", ComponentCategory::Rendering, {
            MakeField("Enabled", &SpriteRendererComponent::Enabled),
            MakeColorField("Color", &SpriteRendererComponent::Color),
            MakeField("Size", &SpriteRendererComponent::Size),
            MakeField("Texture", &SpriteRendererComponent::Texture),
            MakeField("Sort Order", &SpriteRendererComponent::SortOrder),
            MakeField("Flip X", &SpriteRendererComponent::FlipX),
            MakeField("Flip Y", &SpriteRendererComponent::FlipY),
            MakeField("Pivot", &SpriteRendererComponent::Pivot),
            MakeEnumField("Draw Mode", &SpriteRendererComponent::DrawMode, { "Simple", "Sliced" }),
            MakeField("Slice Border", &SpriteRendererComponent::SliceBorder),
        });

        Component<TilemapComponent>("Tilemap", ComponentCategory::Rendering, {
            MakeField("Enabled", &TilemapComponent::Enabled),
            MakeField("Grid Width", &TilemapComponent::GridWidth),
            MakeField("Grid Height", &TilemapComponent::GridHeight),
            MakeField("Cell Size", &TilemapComponent::CellSize),
            MakeField("Tileset", &TilemapComponent::Tileset),
            MakeField("Tile Data", &TilemapComponent::TileData),
            MakeField("Sort Order", &TilemapComponent::SortOrder),
        });

        Component<LineRendererComponent>("Line Renderer", ComponentCategory::Effects, {
            MakeField("Enabled", &LineRendererComponent::Enabled),
            MakeColorField("Color", &LineRendererComponent::Color),
            MakeField("Width", &LineRendererComponent::Width),
            MakeField("Loop", &LineRendererComponent::Loop),
            MakeField("Use World Space", &LineRendererComponent::UseWorldSpace),
            MakeField("Point Count", &LineRendererComponent::PointCount),
            MakeField("Point 0", &LineRendererComponent::Point0),
            MakeField("Point 1", &LineRendererComponent::Point1),
            MakeField("Point 2", &LineRendererComponent::Point2),
            MakeField("Point 3", &LineRendererComponent::Point3),
            MakeField("Point 4", &LineRendererComponent::Point4),
            MakeField("Point 5", &LineRendererComponent::Point5),
            MakeField("Point 6", &LineRendererComponent::Point6),
            MakeField("Point 7", &LineRendererComponent::Point7),
        });

        Component<ParticleSystemComponent>("Particle System", ComponentCategory::Effects, {
            MakeField("Enabled", &ParticleSystemComponent::Enabled),
            MakeField("Texture", &ParticleSystemComponent::Texture),
            MakeField("Playing", &ParticleSystemComponent::Playing),
            MakeField("Loop", &ParticleSystemComponent::Loop),
            MakeField("Emission Rate", &ParticleSystemComponent::EmissionRate),
            MakeField("Max Particles", &ParticleSystemComponent::MaxParticles),
            MakeField("Lifetime", &ParticleSystemComponent::Lifetime),
            MakeField("Start Speed", &ParticleSystemComponent::StartSpeed),
            MakeField("Start Size", &ParticleSystemComponent::StartSize),
            MakeColorField("Start Color", &ParticleSystemComponent::StartColor),
            MakeColorField("End Color", &ParticleSystemComponent::EndColor),
            MakeField("Gravity", &ParticleSystemComponent::Gravity),
            MakeField("Velocity Spread", &ParticleSystemComponent::VelocitySpread),
            MakeField("Sort Order", &ParticleSystemComponent::SortOrder),
        });

        Component<TrailRendererComponent>("Trail Renderer", ComponentCategory::Effects, {
            MakeField("Enabled", &TrailRendererComponent::Enabled),
            Tip(MakeField("Time", &TrailRendererComponent::Time), "Seconds a point stays in the trail."),
            MakeField("Min Vertex Distance", &TrailRendererComponent::MinVertexDistance),
            MakeField("Start Width", &TrailRendererComponent::StartWidth),
            MakeField("End Width", &TrailRendererComponent::EndWidth),
            MakeColorField("Start Color", &TrailRendererComponent::StartColor),
            MakeColorField("End Color", &TrailRendererComponent::EndColor),
            MakeField("Emitting", &TrailRendererComponent::Emitting),
            Tip(MakeField("Autodestruct", &TrailRendererComponent::Autodestruct), "Destroy the entity once the trail is empty and it stopped moving."),
            MakeField("Sort Order", &TrailRendererComponent::SortOrder),
        });

        Component<TextMeshComponent>("Text Mesh", ComponentCategory::Rendering, {
            MakeField("Enabled", &TextMeshComponent::Enabled),
            MakeField("Text", &TextMeshComponent::Text),
            Tip(MakeField("Character Size", &TextMeshComponent::CharacterSize), "Line height = Font Size x Character Size / 100 world units."),
            MakeField("Font Size", &TextMeshComponent::FontSize),
            MakeColorField("Color", &TextMeshComponent::Color),
            MakeEnumField("Anchor", &TextMeshComponent::Anchor,
                { "Upper Left", "Upper Center", "Upper Right", "Middle Left", "Middle Center", "Middle Right", "Lower Left", "Lower Center", "Lower Right" }),
            MakeField("Alignment", &TextMeshComponent::Alignment),
            MakeField("Font", &TextMeshComponent::Font),
            MakeField("Sort Order", &TextMeshComponent::SortOrder),
        });
    }

}
