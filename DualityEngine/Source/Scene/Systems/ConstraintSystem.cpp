// Unity's constraint family (Position / Rotation / Scale / Parent / Look At / Aim), Billboard and
// Trail Renderer point sampling -- evaluated after every script's LateUpdate
// (UpdateLateRuntimeSystems, Scene/SceneRuntimeSystems.h).
#include "DualityEngine/Scene/SceneRuntimeSystems.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {

        // ----------------------------------------------------------- rotation helpers
        // The engine's Euler convention (OpenGLRenderer3D/Citro3DRenderer ComposeWorldMtx and
        // Scene.cpp's Bullet bridge): R = Rz * Ry * Rx, degrees.
        glm::quat QuatFromEuler(const glm::vec3& degrees) {
            const glm::vec3 r = glm::radians(degrees);
            return glm::angleAxis(r.z, glm::vec3(0, 0, 1)) * glm::angleAxis(r.y, glm::vec3(0, 1, 0)) * glm::angleAxis(r.x, glm::vec3(1, 0, 0));
        }

        glm::vec3 EulerFromQuat(const glm::quat& q) {
            const glm::mat3 r = glm::mat3_cast(glm::normalize(q));
            const float y = std::asin(std::clamp(-r[0][2], -1.0f, 1.0f));
            const float x = std::atan2(r[1][2], r[2][2]);
            const float z = std::atan2(r[0][1], r[0][0]);
            return glm::degrees(glm::vec3(x, y, z));
        }

        // Keeps each Euler channel continuous with `reference` (no 359 -> -1 jumps).
        glm::vec3 UnwrapNear(glm::vec3 euler, const glm::vec3& reference) {
            for (int i = 0; i < 3; ++i) {
                while (euler[i] - reference[i] > 180.0f) euler[i] -= 360.0f;
                while (euler[i] - reference[i] < -180.0f) euler[i] += 360.0f;
            }
            return euler;
        }

        glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
            const float length = glm::length(v);
            return length > 1e-6f ? v / length : fallback;
        }

        // Shortest-arc rotation from unit vector a to unit vector b.
        glm::quat RotationBetween(const glm::vec3& a, const glm::vec3& b) {
            const float cosTheta = glm::dot(a, b);
            if (cosTheta < -0.9999f) {
                glm::vec3 axis = glm::cross(glm::vec3(0, 0, 1), a);
                if (glm::dot(axis, axis) < 1e-6f)
                    axis = glm::cross(glm::vec3(1, 0, 0), a);
                return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
            }
            const glm::vec3 axis = glm::cross(a, b);
            const float s = std::sqrt((1.0f + cosTheta) * 2.0f);
            return glm::normalize(glm::quat(s * 0.5f, axis.x / s, axis.y / s, axis.z / s));
        }

        // Rotation taking the orthonormal frame (fromA, fromB) onto (toA, toB) -- A exactly, B
        // as close as possible.
        glm::quat FrameRotation(glm::vec3 fromA, glm::vec3 fromB, glm::vec3 toA, glm::vec3 toB) {
            fromA = SafeNormalize(fromA, glm::vec3(0, 0, -1));
            toA = SafeNormalize(toA, glm::vec3(0, 0, -1));
            auto orthogonal = [](const glm::vec3& a, glm::vec3 b) {
                b -= a * glm::dot(a, b);
                if (glm::dot(b, b) < 1e-8f) {
                    const glm::vec3 helper = std::fabs(a.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    b = helper - a * glm::dot(a, helper);
                }
                return glm::normalize(b);
            };
            fromB = orthogonal(fromA, fromB);
            toB = orthogonal(toA, toB);
            const glm::mat3 from(fromA, fromB, glm::cross(fromA, fromB));
            const glm::mat3 to(toA, toB, glm::cross(toA, toB));
            return glm::quat_cast(to * glm::transpose(from));
        }

        Entity RefEntity(Scene& scene, const EntityRef& ref) {
            if (ref.Handle == EntityRef::Invalid)
                return {};
            Entity entity(static_cast<entt::entity>(ref.Handle), &scene);
            return entity.IsValid() ? entity : Entity{};
        }

        void SetWorld(Scene& scene, Entity entity, const TransformComponent& world, bool position, bool rotation, bool scale) {
            const TransformComponent local = scene.WorldToLocal(entity, world);
            auto& transform = entity.GetComponent<TransformComponent>();
            if (position) transform.Translation = local.Translation;
            if (rotation) transform.Rotation = local.Rotation;
            if (scale) transform.Scale = local.Scale;
        }

        // Average world position / rotation / scale of the valid sources. False if none.
        bool AverageSources(Scene& scene, const std::vector<EntityRef>& sources, glm::vec3* position, glm::quat* rotation, glm::vec3* scale) {
            int count = 0;
            glm::vec3 positionSum(0.0f), scaleProduct(1.0f);
            glm::quat rotationSum(0, 0, 0, 0), first(1, 0, 0, 0);
            for (const EntityRef& ref : sources) {
                Entity source = RefEntity(scene, ref);
                if (!source || !source.HasComponent<TransformComponent>())
                    continue;
                const TransformComponent world = scene.GetWorldTransform(source);
                positionSum += world.Translation;
                scaleProduct *= world.Scale;
                glm::quat q = QuatFromEuler(world.Rotation);
                if (count == 0)
                    first = q;
                else if (glm::dot(first, q) < 0.0f)
                    q = -q; // same hemisphere before summing
                rotationSum = glm::quat(rotationSum.w + q.w, rotationSum.x + q.x, rotationSum.y + q.y, rotationSum.z + q.z);
                ++count;
            }
            if (count == 0)
                return false;
            if (position) *position = positionSum / static_cast<float>(count);
            if (rotation) *rotation = glm::normalize(rotationSum);
            if (scale) *scale = scaleProduct;
            return true;
        }

        glm::vec3 SceneUp() { return glm::vec3(0.0f, -1.0f, 0.0f); } // +Y is down in this engine

        void UpdateConstraints(Scene& scene) {
            auto& registry = scene.Registry();

            for (auto handle : registry.view<PositionConstraintComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& c = registry.get<PositionConstraintComponent>(handle);
                glm::vec3 target;
                if (!c.Enabled || !scene.IsEffectivelyActive(entity) || !AverageSources(scene, c.Sources, &target, nullptr, nullptr))
                    continue;
                TransformComponent world = scene.GetWorldTransform(entity);
                const glm::vec3 blended = glm::mix(c.TranslationAtRest, target + c.TranslationOffset, std::clamp(c.Weight, 0.0f, 1.0f));
                if (c.ConstrainX) world.Translation.x = blended.x;
                if (c.ConstrainY) world.Translation.y = blended.y;
                if (c.ConstrainZ) world.Translation.z = blended.z;
                SetWorld(scene, entity, world, true, false, false);
            }

            for (auto handle : registry.view<RotationConstraintComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& c = registry.get<RotationConstraintComponent>(handle);
                glm::quat target;
                if (!c.Enabled || !scene.IsEffectivelyActive(entity) || !AverageSources(scene, c.Sources, nullptr, &target, nullptr))
                    continue;
                TransformComponent world = scene.GetWorldTransform(entity);
                const glm::quat blended = glm::slerp(QuatFromEuler(c.RotationAtRest), target * QuatFromEuler(c.RotationOffset), std::clamp(c.Weight, 0.0f, 1.0f));
                const glm::vec3 euler = UnwrapNear(EulerFromQuat(blended), world.Rotation);
                if (c.ConstrainX) world.Rotation.x = euler.x;
                if (c.ConstrainY) world.Rotation.y = euler.y;
                if (c.ConstrainZ) world.Rotation.z = euler.z;
                SetWorld(scene, entity, world, false, true, false);
            }

            for (auto handle : registry.view<ScaleConstraintComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& c = registry.get<ScaleConstraintComponent>(handle);
                glm::vec3 target;
                if (!c.Enabled || !scene.IsEffectivelyActive(entity) || !AverageSources(scene, c.Sources, nullptr, nullptr, &target))
                    continue;
                TransformComponent world = scene.GetWorldTransform(entity);
                const glm::vec3 blended = glm::mix(c.ScaleAtRest, target * c.ScaleOffset, std::clamp(c.Weight, 0.0f, 1.0f));
                if (c.ConstrainX) world.Scale.x = blended.x;
                if (c.ConstrainY) world.Scale.y = blended.y;
                if (c.ConstrainZ) world.Scale.z = blended.z;
                SetWorld(scene, entity, world, false, false, true);
            }

            for (auto handle : registry.view<ParentConstraintComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& c = registry.get<ParentConstraintComponent>(handle);
                Entity source = RefEntity(scene, c.Source);
                if (!c.Enabled || !source || source == entity || !scene.IsEffectivelyActive(entity))
                    continue;
                const TransformComponent sourceWorld = scene.GetWorldTransform(source);
                const glm::quat sourceRotation = QuatFromEuler(sourceWorld.Rotation);
                TransformComponent world = scene.GetWorldTransform(entity);
                if (c.AutoOffset && !c.RuntimeCaptured) {
                    const glm::quat inverse = glm::inverse(sourceRotation);
                    c.TranslationOffset = inverse * (world.Translation - sourceWorld.Translation);
                    c.RotationOffset = EulerFromQuat(inverse * QuatFromEuler(world.Rotation));
                    c.RuntimeCaptured = true;
                }
                const float weight = std::clamp(c.Weight, 0.0f, 1.0f);
                if (c.ConstrainTranslation)
                    world.Translation = glm::mix(world.Translation, sourceWorld.Translation + sourceRotation * c.TranslationOffset, weight);
                if (c.ConstrainRotation) {
                    const glm::quat rotation = glm::slerp(QuatFromEuler(world.Rotation), sourceRotation * QuatFromEuler(c.RotationOffset), weight);
                    world.Rotation = UnwrapNear(EulerFromQuat(rotation), world.Rotation);
                }
                SetWorld(scene, entity, world, c.ConstrainTranslation, c.ConstrainRotation, false);
            }

            for (auto handle : registry.view<LookAtConstraintComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& c = registry.get<LookAtConstraintComponent>(handle);
                Entity source = RefEntity(scene, c.Source);
                if (!c.Enabled || !source || source == entity || !scene.IsEffectivelyActive(entity))
                    continue;
                TransformComponent world = scene.GetWorldTransform(entity);
                const glm::vec3 target = scene.GetWorldTransform(source).Translation;
                const glm::vec3 forward = target - world.Translation;
                if (glm::dot(forward, forward) < 1e-10f)
                    continue;
                glm::vec3 up = SceneUp();
                if (c.UseUpObject) {
                    if (Entity upObject = RefEntity(scene, c.WorldUpObject))
                        up = SafeNormalize(scene.GetWorldTransform(upObject).Translation - world.Translation, SceneUp());
                }
                // Local -Z (engine forward) onto `forward`, local up (-Y) towards `up`.
                glm::quat look = FrameRotation(glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), forward, up);
                look = look * glm::angleAxis(glm::radians(c.Roll), glm::vec3(0, 0, -1)) * QuatFromEuler(c.RotationOffset);
                const glm::quat blended = glm::slerp(QuatFromEuler(world.Rotation), look, std::clamp(c.Weight, 0.0f, 1.0f));
                world.Rotation = UnwrapNear(EulerFromQuat(blended), world.Rotation);
                SetWorld(scene, entity, world, false, true, false);
            }

            for (auto handle : registry.view<AimConstraintComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& c = registry.get<AimConstraintComponent>(handle);
                glm::vec3 target;
                if (!c.Enabled || !scene.IsEffectivelyActive(entity) || !AverageSources(scene, c.Sources, &target, nullptr, nullptr))
                    continue;
                TransformComponent world = scene.GetWorldTransform(entity);
                const glm::vec3 direction = target - world.Translation;
                if (glm::dot(direction, direction) < 1e-10f)
                    continue;
                glm::quat aim;
                if (c.WorldUpType == ConstraintWorldUpType::None) {
                    // Shortest arc only -- for 2D (Aim (1,0,0), targets in the XY plane) this
                    // is a pure rotation around Z.
                    aim = RotationBetween(SafeNormalize(c.AimVector, glm::vec3(1, 0, 0)), glm::normalize(direction));
                } else {
                    glm::vec3 worldUp = SceneUp();
                    if (c.WorldUpType == ConstraintWorldUpType::Vector)
                        worldUp = c.WorldUpVector;
                    else if (Entity upObject = RefEntity(scene, c.WorldUpObject)) {
                        const TransformComponent upWorld = scene.GetWorldTransform(upObject);
                        worldUp = c.WorldUpType == ConstraintWorldUpType::ObjectUp
                            ? upWorld.Translation - world.Translation
                            : QuatFromEuler(upWorld.Rotation) * c.WorldUpVector;
                    }
                    aim = FrameRotation(c.AimVector, c.UpVector, direction, worldUp);
                }
                aim = aim * QuatFromEuler(c.RotationOffset);
                const glm::quat blended = glm::slerp(QuatFromEuler(world.Rotation), aim, std::clamp(c.Weight, 0.0f, 1.0f));
                world.Rotation = UnwrapNear(EulerFromQuat(blended), world.Rotation);
                SetWorld(scene, entity, world, false, true, false);
            }
        }

        void UpdateBillboards(Scene& scene) {
            auto& registry = scene.Registry();
            for (auto handle : registry.view<BillboardComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& billboard = registry.get<BillboardComponent>(handle);
                if (!billboard.Enabled || !scene.IsEffectivelyActive(entity))
                    continue;
                Screen screen = Screen::Top;
                scene.TryResolveEntityScreen(entity, screen);
                Entity camera = scene.GetPrimaryCamera(screen);
                if (!camera)
                    continue;
                const TransformComponent cameraWorld = scene.GetWorldTransform(camera);
                TransformComponent world = scene.GetWorldTransform(entity);
                glm::quat rotation;
                if (billboard.Mode == BillboardMode::AlignWithCamera) {
                    rotation = QuatFromEuler(cameraWorld.Rotation);
                } else {
                    // Local +Z towards the camera, i.e. forward (-Z) away from it.
                    const glm::vec3 away = world.Translation - cameraWorld.Translation;
                    if (glm::dot(away, away) < 1e-10f)
                        continue;
                    rotation = FrameRotation(glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), away, SceneUp());
                }
                glm::vec3 euler = UnwrapNear(EulerFromQuat(rotation), world.Rotation);
                if (billboard.LockYAxis) {
                    // Only the heading: rotate around world up.
                    const glm::vec3 forward = rotation * glm::vec3(0, 0, -1);
                    const glm::vec3 flat(forward.x, 0.0f, forward.z);
                    if (glm::dot(flat, flat) < 1e-10f)
                        continue;
                    const glm::quat heading = FrameRotation(glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), flat, SceneUp());
                    euler = UnwrapNear(EulerFromQuat(heading), world.Rotation);
                }
                world.Rotation = euler;
                SetWorld(scene, entity, world, false, true, false);
            }
        }

        void UpdateTrails(Scene& scene, float deltaTime) {
            auto& registry = scene.Registry();
            for (auto handle : registry.view<TrailRendererComponent, TransformComponent>()) {
                Entity entity(handle, &scene);
                auto& trail = registry.get<TrailRendererComponent>(handle);
                if (!trail.Enabled || !scene.IsEffectivelyActive(entity))
                    continue;
                trail.RuntimeClock += std::max(deltaTime, 0.0f);
                const float lifetime = std::max(trail.Time, 0.0f);
                while (trail.RuntimeCount > 0 && trail.RuntimeClock - trail.RuntimeTimes[trail.RuntimeHead] > lifetime) {
                    trail.RuntimeHead = (trail.RuntimeHead + 1) % MaxTrailPoints;
                    --trail.RuntimeCount;
                }
                if (trail.Emitting) {
                    const TransformComponent world = scene.GetWorldTransform(entity);
                    const glm::vec2 position(world.Translation.x, world.Translation.y);
                    bool add = trail.RuntimeCount == 0;
                    if (!add) {
                        const int last = (trail.RuntimeHead + trail.RuntimeCount - 1) % MaxTrailPoints;
                        add = glm::distance(trail.RuntimePoints[last], position) >= std::max(trail.MinVertexDistance, 0.0001f);
                    }
                    if (add) {
                        if (trail.RuntimeCount == MaxTrailPoints) {
                            trail.RuntimeHead = (trail.RuntimeHead + 1) % MaxTrailPoints;
                            --trail.RuntimeCount;
                        }
                        const int slot = (trail.RuntimeHead + trail.RuntimeCount) % MaxTrailPoints;
                        trail.RuntimePoints[slot] = position;
                        trail.RuntimeTimes[slot] = trail.RuntimeClock;
                        ++trail.RuntimeCount;
                    }
                } else if (trail.Autodestruct && trail.RuntimeCount == 0) {
                    scene.DestroyEntityDeferred(entity);
                }
            }
        }
    }

    void UpdateLateRuntimeSystems(Scene& scene, float deltaTime) {
        UpdateConstraints(scene);
        UpdateBillboards(scene);
        UpdateTrails(scene, deltaTime);
    }

}
