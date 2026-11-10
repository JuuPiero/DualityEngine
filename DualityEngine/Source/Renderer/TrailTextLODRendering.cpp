// Trail Renderer and Text Mesh (2D world pass) and LOD Group selection (3D mesh pass) -- the
// world-space components SceneRenderer's main loops delegate here.
#include "DualityEngine/Renderer/TrailTextLODRendering.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Renderer/IRenderer2D.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        uint32_t ResolveFontId(IRenderer2D& renderer, const AssetRef& font) {
            if (font.Guid.empty())
                return 0;
            const std::string path = AssetDatabase::ResolvePath(font.Guid);
            return path.empty() ? 0 : renderer.LoadFont(path);
        }

        // One segment of a ribbon: a rotated quad from a to b.
        void DrawSegment(IRenderer2D& renderer, const glm::vec2& a, const glm::vec2& b, float width, const glm::vec4& color) {
            const glm::vec2 delta = b - a;
            const float length = glm::length(delta);
            if (length < 0.01f || width <= 0.0f || color.a <= 0.0f)
                return;
            const glm::vec2 center = (a + b) * 0.5f;
            const float angle = glm::degrees(std::atan2(delta.y, delta.x));
            // Slight overlap hides the gaps between consecutive segments at corners.
            const float drawLength = length + width * 0.5f;
            renderer.DrawQuad({ center.x - drawLength * 0.5f, center.y - width * 0.5f }, { drawLength, width }, color, angle);
        }

        struct WorldExtraItem {
            entt::entity Handle;
            int SortOrder;
            bool IsTrail;
        };

        void DrawTrail(IRenderer2D& renderer, Scene& scene, entt::entity handle, const glm::vec2& origin, float scale, const glm::vec2& offset) {
            auto& trail = scene.Registry().get<TrailRendererComponent>(handle);
            if (trail.RuntimeCount == 0)
                return;
            const float lifetime = std::max(trail.Time, 0.0001f);
            auto toScreen = [&](const glm::vec2& p) { return (p - origin) * scale + offset; };
            glm::vec2 previous{};
            float previousAge = 0.0f;
            bool hasPrevious = false;
            // Newest first: the entity's current position (if still emitting), then the samples.
            if (trail.Emitting) {
                const TransformComponent world = scene.GetWorldTransform(Entity(handle, &scene));
                previous = toScreen({ world.Translation.x, world.Translation.y });
                hasPrevious = true;
            }
            for (int i = trail.RuntimeCount - 1; i >= 0; --i) {
                const int slot = (trail.RuntimeHead + i) % MaxTrailPoints;
                const glm::vec2 point = toScreen(trail.RuntimePoints[slot]);
                const float age = std::clamp((trail.RuntimeClock - trail.RuntimeTimes[slot]) / lifetime, 0.0f, 1.0f);
                if (hasPrevious) {
                    const float u = (age + previousAge) * 0.5f;
                    const float width = glm::mix(trail.StartWidth, trail.EndWidth, u) * scale;
                    DrawSegment(renderer, previous, point, width, glm::mix(trail.StartColor, trail.EndColor, u));
                }
                previous = point;
                previousAge = age;
                hasPrevious = true;
            }
        }

        void DrawTextMesh(IRenderer2D& renderer, Scene& scene, entt::entity handle, const glm::vec2& origin, float scale, const glm::vec2& offset) {
            auto& textMesh = scene.Registry().get<TextMeshComponent>(handle);
            if (textMesh.Text.empty() || textMesh.Color.a <= 0.0f)
                return;
            const TransformComponent world = scene.GetWorldTransform(Entity(handle, &scene));
            const float lineHeight = textMesh.FontSize * textMesh.CharacterSize / 100.0f * scale * std::fabs(world.Scale.y);
            if (lineHeight <= 0.01f)
                return;
            const uint32_t fontId = ResolveFontId(renderer, textMesh.Font);
            const glm::vec2 anchor = (glm::vec2(world.Translation.x, world.Translation.y) - origin) * scale + offset;

            // Lines and block size.
            int lineCount = 1;
            for (char ch : textMesh.Text)
                if (ch == '\n') ++lineCount;
            float blockWidth = 0.0f;
            size_t start = 0;
            for (int line = 0; line < lineCount; ++line) {
                const size_t end = textMesh.Text.find('\n', start);
                const std::string part = textMesh.Text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                blockWidth = std::max(blockWidth, renderer.MeasureText(part, lineHeight, fontId).x);
                start = end == std::string::npos ? textMesh.Text.size() : end + 1;
            }
            const float blockHeight = lineHeight * lineCount;
            const int anchorIndex = static_cast<int>(textMesh.Anchor);
            const int column = anchorIndex % 3, row = anchorIndex / 3;
            const glm::vec2 blockTopLeft{ anchor.x - blockWidth * 0.5f * column, anchor.y - blockHeight * 0.5f * row };

            start = 0;
            for (int line = 0; line < lineCount; ++line) {
                const size_t end = textMesh.Text.find('\n', start);
                const std::string part = textMesh.Text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                start = end == std::string::npos ? textMesh.Text.size() : end + 1;
                const float width = renderer.MeasureText(part, lineHeight, fontId).x;
                float x = blockTopLeft.x;
                if (textMesh.Alignment == TextAlignment::Center) x += (blockWidth - width) * 0.5f;
                else if (textMesh.Alignment == TextAlignment::Right) x += blockWidth - width;
                renderer.DrawText(part, { x, blockTopLeft.y + lineHeight * line }, lineHeight, textMesh.Color, fontId);
            }
        }

        void SetHidden(Scene& scene, Entity root, bool hidden) {
            if (!root)
                return;
            auto& registry = scene.Registry();
            if (hidden)
                registry.emplace_or_replace<LODHiddenTag>(root.Handle());
            else if (registry.all_of<LODHiddenTag>(root.Handle()))
                registry.remove<LODHiddenTag>(root.Handle());
            if (auto* hierarchy = registry.try_get<HierarchyComponent>(root.Handle()))
                for (Entity child : hierarchy->Children)
                    SetHidden(scene, child, hidden);
        }

        Entity LODEntity(Scene& scene, const EntityRef& ref) {
            if (ref.Handle == EntityRef::Invalid)
                return {};
            Entity entity(static_cast<entt::entity>(ref.Handle), &scene);
            return entity.IsValid() ? entity : Entity{};
        }

        // Option `index` of a '|' separated list.
    }


    void RenderWorldExtras2D(IRenderer2D& renderer, Scene& scene, Screen screen, const CameraComponent* camera,
                             const glm::vec2& cameraOrigin, float scale, const glm::vec2& screenOffset) {
        auto& registry = scene.Registry();
        auto trails = registry.view<TrailRendererComponent>();
        auto texts = registry.view<TextMeshComponent>();
        if (trails.empty() && texts.empty())
            return;
        static std::vector<WorldExtraItem> items; // reused: no allocation once warmed up
        items.clear();
        for (auto handle : trails) {
            const auto& trail = trails.get<TrailRendererComponent>(handle);
            if (trail.Enabled && trail.RuntimeCount > 0 && ShouldRenderOnScreen(scene, handle, screen, camera) &&
                scene.IsEffectivelyActive(Entity(handle, &scene)))
                items.push_back({ handle, trail.SortOrder, true });
        }
        for (auto handle : texts) {
            const auto& text = texts.get<TextMeshComponent>(handle);
            if (text.Enabled && registry.all_of<TransformComponent>(handle) && ShouldRenderOnScreen(scene, handle, screen, camera) &&
                scene.IsEffectivelyActive(Entity(handle, &scene)))
                items.push_back({ handle, text.SortOrder, false });
        }
        std::stable_sort(items.begin(), items.end(), [](const WorldExtraItem& a, const WorldExtraItem& b) { return a.SortOrder < b.SortOrder; });
        for (const WorldExtraItem& item : items) {
            if (item.IsTrail)
                DrawTrail(renderer, scene, item.Handle, cameraOrigin, scale, screenOffset);
            else
                DrawTextMesh(renderer, scene, item.Handle, cameraOrigin, scale, screenOffset);
        }
    }

    void ApplyLODGroups(Scene& scene, const glm::vec3& cameraPosition) {
        auto& registry = scene.Registry();
        auto groups = registry.view<LODGroupComponent, TransformComponent>();
        if (groups.begin() == groups.end()) {
            // A removed LOD Group must not leave its renderers hidden.
            if (!registry.view<LODHiddenTag>().empty())
                registry.clear<LODHiddenTag>();
            return;
        }
        for (auto handle : groups) {
            Entity entity(handle, &scene);
            auto& group = groups.get<LODGroupComponent>(handle);
            const EntityRef* slots[4] = { &group.LOD0, &group.LOD1, &group.LOD2, &group.LOD3 };
            int level;
            if (!group.Enabled || !scene.IsEffectivelyActive(entity)) {
                level = -2; // everything visible, as authored
            } else {
                const float bias = std::max(group.LODBias, 0.01f);
                const float distance = glm::distance(scene.GetWorldTransform(entity).Translation, cameraPosition) / bias;
                level = distance < group.Distance0 ? 0 : distance < group.Distance1 ? 1 : distance < group.Distance2 ? 2 : 3;
                // An unassigned slot falls back to the closest coarser-to-finer assigned one.
                while (level > 0 && slots[level]->Handle == EntityRef::Invalid)
                    --level;
                if (group.CullDistance > 0.0f && distance >= group.CullDistance)
                    level = -1;
            }
            if (level == group.CurrentLOD)
                continue;
            group.CurrentLOD = level;
            // Hide first, then show the chosen level (a renderer shared by two levels stays visible).
            for (int i = 0; i < 4; ++i)
                if (level != -2 && i != level)
                    SetHidden(scene, LODEntity(scene, *slots[i]), true);
            for (int i = 0; i < 4; ++i)
                if (level == -2 || i == level)
                    SetHidden(scene, LODEntity(scene, *slots[i]), false);
        }
    }

}
