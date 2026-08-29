#include "DualityEngine/Scene/SceneRuntimeSystems.h"

#include <cmath>
#include <fstream>
#include <random>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/UI/UIDocument.h"
#include "DualityEngine/UI/UIDocumentLoader.h"

namespace Duality {

    namespace {
        std::unordered_map<entt::entity, std::vector<Particle>> s_ParticlePools;
        std::mt19937 s_Rng{ 42 };

        float RandRange(float minV, float maxV) {
            std::uniform_real_distribution<float> dist(minV, maxV);
            return dist(s_Rng);
        }

        void ApplyUILayoutRecursive(Scene& scene, Entity entity) {
            if (!entity.HasComponent<UILayoutGroupComponent>() || !entity.HasComponent<UIRectComponent>())
                return;
            auto& layout = entity.GetComponent<UILayoutGroupComponent>();
            auto& hierarchy = entity.GetComponent<HierarchyComponent>();

            // The parent's own resolved size may differ from its raw SizeDelta if it's stretch-
            // anchored -- must resolve it for real rather than reading a raw field.
            glm::vec2 parentTopLeft, parentSize;
            ResolveUIRect(scene, entity, parentTopLeft, parentSize);

            float cursor = layout.Layout == UILayoutType::Vertical ? layout.Padding.y : layout.Padding.x;
            for (Entity child : hierarchy.Children) {
                if (!child || !child.HasComponent<UIRectComponent>())
                    continue;
                auto& childRect = child.GetComponent<UIRectComponent>();
                // A layout group takes full control of every managed child's anchor, matching
                // Unity's own LayoutGroup (SetInsetAndSizeFromParentEdge forces
                // anchorMin=anchorMax=pivot to the controlled corner unconditionally on every
                // rebuild) -- re-forced every pass, not just once, so AnchoredPosition plays
                // exactly the role the old top-left-relative Offset used to.
                childRect.AnchorMin = childRect.AnchorMax = childRect.Pivot = { 0.0f, 0.0f };
                if (layout.Layout == UILayoutType::Vertical) {
                    if (layout.ChildControlWidth)
                        childRect.SizeDelta.x = parentSize.x - layout.Padding.x * 2.0f;
                    childRect.AnchoredPosition.y = cursor;
                    childRect.AnchoredPosition.x = layout.Padding.x;
                    cursor += childRect.SizeDelta.y + layout.Spacing;
                } else {
                    if (layout.ChildControlHeight)
                        childRect.SizeDelta.y = parentSize.y - layout.Padding.y * 2.0f;
                    childRect.AnchoredPosition.x = cursor;
                    childRect.AnchoredPosition.y = layout.Padding.y;
                    cursor += childRect.SizeDelta.x + layout.Spacing;
                }
                ApplyUILayoutRecursive(scene, child);
            }
        }

        void EmitParticle(ParticleSystemComponent& sys, std::vector<Particle>& pool, const glm::vec2& origin) {
            if (static_cast<int>(pool.size()) >= sys.MaxParticles)
                return;
            Particle p;
            p.Position = origin;
            p.Velocity = { RandRange(-sys.VelocitySpread.x, sys.VelocitySpread.x),
                           RandRange(-sys.VelocitySpread.y, sys.VelocitySpread.y) - sys.StartSpeed };
            p.MaxLife = sys.Lifetime * RandRange(0.8f, 1.2f);
            p.Life = p.MaxLife;
            p.Size = sys.StartSize;
            p.Color = sys.StartColor;
            pool.push_back(p);
            sys.AliveCount = static_cast<int>(pool.size());
        }
    }

    TilemapData TilemapLoader::Load(const std::string& path) {
        TilemapData data;
        std::ifstream file(path);
        if (!file.is_open())
            return data;
        try {
            nlohmann::json j;
            file >> j;
            data.Width = j.value("Width", 0);
            data.Height = j.value("Height", 0);
            if (j.contains("Tiles") && j["Tiles"].is_array())
                for (auto& t : j["Tiles"])
                    data.Tiles.push_back(t.get<int>());
        } catch (...) {
        }
        return data;
    }

    std::vector<Particle>& GetParticlePool(entt::entity handle) {
        return s_ParticlePools[handle];
    }

    void ClearSceneRuntimeSystems(Scene& scene) {
        (void)scene;
        s_ParticlePools.clear();
    }

    void UpdateSceneRuntimeSystems(Scene& scene, float deltaTime) {
        for (auto handle : scene.Registry().view<UIDocumentReferenceComponent>()) {
            auto& docRef = scene.Registry().get<UIDocumentReferenceComponent>(handle);
            if (!docRef.Enabled || !docRef.InstantiateOnPlay || docRef.Instantiated || docRef.Document.Guid.empty())
                continue;
            std::string path = AssetDatabase::ResolvePath(docRef.Document.Guid);
            if (path.empty())
                continue;
            const UIDocument& doc = UIDocumentLoader::Load(path);
            if (doc.IsLoaded()) {
                doc.Instantiate(scene, Screen::Bottom);
                docRef.Instantiated = true;
            }
        }

        for (auto handle : scene.Registry().view<UILayoutGroupComponent, UIRectComponent, HierarchyComponent>()) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            if (!scene.Registry().get<UILayoutGroupComponent>(handle).Enabled)
                continue;
            ApplyUILayoutRecursive(scene, Entity(handle, &scene));
        }

        for (auto handle : scene.Registry().view<FollowTargetComponent, TransformComponent>()) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& follow = scene.Registry().get<FollowTargetComponent>(handle);
            if (!follow.Enabled)
                continue;
            if (follow.Target.Handle == EntityRef::Invalid)
                continue;
            Entity target(static_cast<entt::entity>(follow.Target.Handle), &scene);
            if (!target)
                continue;
            auto& transform = scene.Registry().get<TransformComponent>(handle);
            glm::vec3 targetPos = scene.GetWorldTransform(target).Translation + follow.Offset;
            auto lerp = [&](float current, float goal) {
                if (follow.SmoothSpeed <= 0.0f)
                    return goal;
                float t = 1.0f - std::exp(-follow.SmoothSpeed * deltaTime);
                return current + (goal - current) * t;
            };
            if (follow.FollowX) transform.Translation.x = lerp(transform.Translation.x, targetPos.x);
            if (follow.FollowY) transform.Translation.y = lerp(transform.Translation.y, targetPos.y);
            if (follow.FollowZ) transform.Translation.z = lerp(transform.Translation.z, targetPos.z);
        }

        for (auto handle : scene.Registry().view<SpriteSheetAnimatorComponent>()) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& anim = scene.Registry().get<SpriteSheetAnimatorComponent>(handle);
            if (!anim.Enabled || !anim.Playing || anim.Columns <= 0 || anim.Rows <= 0)
                continue;
            int frameCount = anim.Columns * anim.Rows;
            anim.ElapsedTime += deltaTime;
            float frameDuration = anim.FrameRate > 0.0f ? (1.0f / anim.FrameRate) : 0.1f;
            while (anim.ElapsedTime >= frameDuration) {
                anim.ElapsedTime -= frameDuration;
                anim.CurrentFrame++;
                if (anim.CurrentFrame >= frameCount) {
                    if (anim.Loop)
                        anim.CurrentFrame = 0;
                    else {
                        anim.CurrentFrame = frameCount - 1;
                        anim.Playing = false;
                        break;
                    }
                }
            }
        }

        for (auto handle : scene.Registry().view<ParticleSystemComponent, TransformComponent>()) {
            if (!scene.IsEffectivelyActive(Entity(handle, &scene)))
                continue;
            auto& sys = scene.Registry().get<ParticleSystemComponent>(handle);
            if (!sys.Enabled)
                continue;
            auto& transform = scene.Registry().get<TransformComponent>(handle);
            glm::vec2 origin{ transform.Translation.x, transform.Translation.y };
            auto& pool = s_ParticlePools[handle];

            if (sys.Playing) {
                sys.EmissionAccumulator += sys.EmissionRate * deltaTime;
                while (sys.EmissionAccumulator >= 1.0f) {
                    sys.EmissionAccumulator -= 1.0f;
                    EmitParticle(sys, pool, origin);
                }
            }

            for (auto it = pool.begin(); it != pool.end();) {
                it->Life -= deltaTime;
                if (it->Life <= 0.0f) {
                    it = pool.erase(it);
                    continue;
                }
                float t = 1.0f - (it->Life / it->MaxLife);
                it->Velocity += sys.Gravity * deltaTime;
                it->Position += it->Velocity * deltaTime;
                it->Color = glm::mix(sys.EndColor, sys.StartColor, 1.0f - t);
                it->Size = sys.StartSize * (1.0f - t * 0.5f);
                ++it;
            }
            sys.AliveCount = static_cast<int>(pool.size());
            if (!sys.Playing && !sys.Loop && pool.empty())
                sys.Playing = false;
        }
    }

}
