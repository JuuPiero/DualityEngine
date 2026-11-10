#include "DualityEditor/SceneView/ComponentGizmos.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/MeshReference.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    namespace {
        const ImU32 ColliderColor = IM_COL32(60, 230, 90, 255);
        const ImU32 AnchorColor = IM_COL32(80, 170, 255, 255);
        const ImU32 ConnectedColor = IM_COL32(120, 230, 255, 255);
        const ImU32 EffectorColor = IM_COL32(150, 200, 255, 220);
        const ImU32 LimitColor = IM_COL32(255, 200, 80, 220);
        const ImU32 ControllerColor = IM_COL32(60, 230, 90, 255);
        constexpr float HandleHalf = 5.0f;

        glm::vec2 Rotate2D(const glm::vec2& v, float degrees) {
            const float r = glm::radians(degrees);
            const float c = std::cos(r), s = std::sin(r);
            return { v.x * c - v.y * s, v.x * s + v.y * c };
        }

        // Unity angles (0 = +X, 90 = up) in the +Y-down world.
        glm::vec2 Direction2D(float degrees) {
            const float r = glm::radians(degrees);
            return { std::cos(r), -std::sin(r) };
        }

        glm::quat EulerToQuat(const glm::vec3& degrees) {
            const glm::vec3 r = glm::radians(degrees);
            return glm::angleAxis(r.z, glm::vec3(0, 0, 1)) * glm::angleAxis(r.y, glm::vec3(0, 1, 0)) * glm::angleAxis(r.x, glm::vec3(1, 0, 0));
        }

        // A square handle at `at`; returns the mouse movement (screen pixels) while dragged.
        bool DragHandle(const char* id, ImDrawList* drawList, ImVec2 at, ImU32 color, ImVec2& outDelta) {
            drawList->AddRectFilled(ImVec2(at.x - HandleHalf, at.y - HandleHalf), ImVec2(at.x + HandleHalf, at.y + HandleHalf), color);
            ImGui::SetCursorScreenPos(ImVec2(at.x - HandleHalf, at.y - HandleHalf));
            ImGui::InvisibleButton(id, ImVec2(HandleHalf * 2.0f, HandleHalf * 2.0f));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                drawList->AddRect(ImVec2(at.x - HandleHalf - 2, at.y - HandleHalf - 2), ImVec2(at.x + HandleHalf + 2, at.y + HandleHalf + 2), IM_COL32(255, 255, 255, 220));
            if (!ImGui::IsItemActive() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                return false;
            outDelta = ImGui::GetIO().MouseDelta;
            return outDelta.x != 0.0f || outDelta.y != 0.0f;
        }

        void Arrow(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float thickness = 2.0f) {
            drawList->AddLine(from, to, color, thickness);
            const glm::vec2 d{ to.x - from.x, to.y - from.y };
            const float length = glm::length(d);
            if (length < 1.0f)
                return;
            const glm::vec2 u = d / length;
            const glm::vec2 n{ -u.y, u.x };
            const float head = std::min(9.0f, length * 0.4f);
            drawList->AddTriangleFilled(to,
                ImVec2(to.x - u.x * head + n.x * head * 0.5f, to.y - u.y * head + n.y * head * 0.5f),
                ImVec2(to.x - u.x * head - n.x * head * 0.5f, to.y - u.y * head - n.y * head * 0.5f), color);
        }

        // ---------------------------------------------------------------------- 2D joints
        struct JointPoints2D { glm::vec2 Self, Other; bool HasConnected; float SelfRotation; };

        // Mirrors Physics2DExtensions.cpp: Distance / Spring / Slider / Wheel joints auto-configure
        // the connected anchor at the connected body's origin, pivot joints at the anchor.
        bool AutoAtConnectedOrigin(Entity self, const Joint2DBase& joint) {
            const void* p = &joint;
            return (self.HasComponent<DistanceJoint2DComponent>() && p == &self.GetComponent<DistanceJoint2DComponent>()) ||
                (self.HasComponent<SpringJoint2DComponent>() && p == &self.GetComponent<SpringJoint2DComponent>()) ||
                (self.HasComponent<SliderJoint2DComponent>() && p == &self.GetComponent<SliderJoint2DComponent>()) ||
                (self.HasComponent<WheelJoint2DComponent>() && p == &self.GetComponent<WheelJoint2DComponent>());
        }

        JointPoints2D ResolveJoint2D(EditorContext& ctx, Entity self, const Joint2DBase& joint) {
            JointPoints2D points{};
            const TransformComponent world = ctx.SceneRef.GetWorldTransform(self);
            points.SelfRotation = world.Rotation.z;
            points.Self = glm::vec2(world.Translation) + Rotate2D(joint.Anchor, world.Rotation.z);
            points.Other = points.Self;
            Entity connected;
            if (joint.ConnectedBody.Handle != EntityRef::Invalid)
                connected = Entity(static_cast<entt::entity>(joint.ConnectedBody.Handle), &ctx.SceneRef);
            points.HasConnected = connected.IsValid();
            if (!joint.AutoConfigureConnectedAnchor) {
                if (points.HasConnected) {
                    const TransformComponent other = ctx.SceneRef.GetWorldTransform(connected);
                    points.Other = glm::vec2(other.Translation) + Rotate2D(joint.ConnectedAnchor, other.Rotation.z);
                } else {
                    points.Other = joint.ConnectedAnchor;
                }
            } else if (points.HasConnected && AutoAtConnectedOrigin(self, joint)) {
                points.Other = glm::vec2(ctx.SceneRef.GetWorldTransform(connected).Translation);
            } else if (points.HasConnected) {
                // Auto-configured pivot: the connected anchor sits on the anchor.
                points.Other = points.Self;
            }
            return points;
        }

        // Anchor dot (draggable: edits Anchor) + connected anchor + link line. Returns true if
        // the anchor was edited.
        bool DrawJointAnchors2D(EditorContext& ctx, ImDrawList* drawList, Entity self, Joint2DBase& joint, const char* id,
                                const std::function<ImVec2(const glm::vec2&)>& toScreen, float worldToPixels, JointPoints2D& points) {
            points = ResolveJoint2D(ctx, self, joint);
            const ImVec2 selfScreen = toScreen(points.Self);
            const ImVec2 otherScreen = toScreen(points.Other);
            if (points.HasConnected) {
                const TransformComponent other = ctx.SceneRef.GetWorldTransform(Entity(static_cast<entt::entity>(joint.ConnectedBody.Handle), &ctx.SceneRef));
                drawList->AddLine(toScreen(glm::vec2(other.Translation)), otherScreen, IM_COL32(120, 230, 255, 120), 1.0f);
            }
            drawList->AddLine(selfScreen, otherScreen, ConnectedColor, 1.5f);
            drawList->AddCircle(otherScreen, 6.0f, ConnectedColor, 16, 2.0f);
            ImVec2 delta;
            bool changed = false;
            if (DragHandle(id, drawList, selfScreen, AnchorColor, delta)) {
                joint.Anchor += Rotate2D(glm::vec2(delta.x, delta.y) / worldToPixels, -points.SelfRotation);
                changed = true;
            }
            drawList->AddCircle(selfScreen, 7.0f, AnchorColor, 16, 2.0f);
            return changed;
        }

        void Arc2D(ImDrawList* drawList, ImVec2 center, float radius, float fromDegrees, float toDegrees, ImU32 color) {
            // Degrees in the screen's own rotation sense (+ = clockwise on a Y-down screen),
            // matching TransformComponent::Rotation.z.
            const int segments = std::max(4, static_cast<int>(std::fabs(toDegrees - fromDegrees) / 10.0f));
            ImVec2 previous{};
            for (int i = 0; i <= segments; ++i) {
                const float a = glm::radians(fromDegrees + (toDegrees - fromDegrees) * i / segments);
                const ImVec2 p(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius);
                if (i > 0)
                    drawList->AddLine(previous, p, color, 2.0f);
                previous = p;
            }
            drawList->AddLine(center, previous, color, 1.0f);
        }

        template<typename T>
        T* SelectedComponent(EditorContext& ctx, Screen screen) {
            if (!ctx.Selected || !ShouldRenderOnScreen(ctx.SceneRef, ctx.Selected.Handle(), screen))
                return nullptr;
            T* component = ctx.Selected.TryGetComponent<T>();
            return component && component->Enabled ? component : nullptr;
        }

        // Approximate half-size of an entity's 2D colliders, for effector gizmos.
        glm::vec2 ColliderHalfExtent2D(Entity entity) {
            if (auto* box = entity.TryGetComponent<BoxCollider2DComponent>())
                return box->Size;
            if (auto* circle = entity.TryGetComponent<CircleCollider2DComponent>())
                return glm::vec2(circle->Radius);
            if (auto* capsule = entity.TryGetComponent<CapsuleCollider2DComponent>())
                return { capsule->Radius, capsule->Height * 0.5f };
            return glm::vec2(0.16f);
        }

        // ---------------------------------------------------------------------- 3D helpers
        void Line3D(ImDrawList* drawList, const std::function<bool(const glm::vec3&, ImVec2&)>& project, const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness = 1.5f) {
            ImVec2 sa, sb;
            if (project(a, sa) && project(b, sb))
                drawList->AddLine(sa, sb, color, thickness);
        }

        void Circle3D(ImDrawList* drawList, const std::function<bool(const glm::vec3&, ImVec2&)>& project, const glm::vec3& center,
                      const glm::vec3& u, const glm::vec3& v, float radius, ImU32 color, float fromRadians = 0.0f, float toRadians = glm::two_pi<float>()) {
            constexpr int Segments = 24;
            glm::vec3 previous = center + (u * std::cos(fromRadians) + v * std::sin(fromRadians)) * radius;
            for (int i = 1; i <= Segments; ++i) {
                const float a = fromRadians + (toRadians - fromRadians) * i / Segments;
                const glm::vec3 p = center + (u * std::cos(a) + v * std::sin(a)) * radius;
                Line3D(drawList, project, previous, p, color);
                previous = p;
            }
        }

        template<typename T>
        void DrawJoint3D(EditorContext& ctx, Screen screen, ImDrawList* drawList, const std::function<bool(const glm::vec3&, ImVec2&)>& project,
                         const glm::vec3* axis) {
            T* joint = SelectedComponent<T>(ctx, screen);
            if (!joint)
                return;
            const TransformComponent world = ctx.SceneRef.GetWorldTransform(ctx.Selected);
            const glm::quat rotation = EulerToQuat(world.Rotation);
            const glm::vec3 anchor = world.Translation + rotation * joint->Anchor;
            glm::vec3 other = anchor;
            if (joint->ConnectedBody.Handle != EntityRef::Invalid) {
                Entity connected(static_cast<entt::entity>(joint->ConnectedBody.Handle), &ctx.SceneRef);
                if (connected) {
                    const TransformComponent otherWorld = ctx.SceneRef.GetWorldTransform(connected);
                    other = joint->AutoConfigureConnectedAnchor ? otherWorld.Translation
                        : otherWorld.Translation + EulerToQuat(otherWorld.Rotation) * joint->ConnectedAnchor;
                }
            } else if (!joint->AutoConfigureConnectedAnchor) {
                other = joint->ConnectedAnchor;
            }
            Line3D(drawList, project, anchor, other, ConnectedColor);
            ImVec2 s;
            if (project(anchor, s))
                drawList->AddCircleFilled(s, 5.0f, AnchorColor, 12);
            if (project(other, s))
                drawList->AddCircle(s, 6.0f, ConnectedColor, 12, 2.0f);
            if (axis) {
                const glm::vec3 a = rotation * glm::normalize(glm::length(*axis) > 1e-5f ? *axis : glm::vec3(1, 0, 0));
                ImVec2 from, to;
                const glm::vec3 tip = anchor + a * 0.6f;
                if (project(anchor, from) && project(tip, to))
                    Arrow(drawList, from, to, LimitColor);
            }
        }
    }

    void DrawComponentGizmos2D(EditorContext& ctx, Screen screen, ImDrawList* drawList,
                              const std::function<ImVec2(const glm::vec2&)>& toScreen, float worldToPixels) {
        if (!ctx.Selected || worldToPixels <= 0.0f)
            return;
        Entity selected = ctx.Selected;
        const TransformComponent world = ctx.SceneRef.GetWorldTransform(selected);
        const glm::vec2 position(world.Translation);
        bool changed = false;

        // Edge Collider 2D: polyline + one draggable handle per point.
        if (auto* edge = SelectedComponent<EdgeCollider2DComponent>(ctx, screen)) {
            const int count = std::clamp(edge->PointCount, 2, MaxEdgeCollider2DPoints);
            ImVec2 screenPoints[MaxEdgeCollider2DPoints];
            for (int i = 0; i < count; ++i)
                screenPoints[i] = toScreen(position + Rotate2D(GetEdgePoint2D(*edge, i) + edge->Offset, world.Rotation.z));
            drawList->AddPolyline(screenPoints, count, ColliderColor, 0, std::max(2.0f, edge->EdgeRadius * worldToPixels * 2.0f));
            for (int i = 0; i < count; ++i) {
                ImVec2 delta;
                char id[32];
                std::snprintf(id, sizeof(id), "##EdgePoint%d", i);
                if (DragHandle(id, drawList, screenPoints[i], ColliderColor, delta)) {
                    GetEdgePoint2D(*edge, i) += Rotate2D(glm::vec2(delta.x, delta.y) / worldToPixels, -world.Rotation.z);
                    changed = true;
                }
            }
        }

        JointPoints2D points{};
        if (auto* j = SelectedComponent<DistanceJoint2DComponent>(ctx, screen)) {
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##DistanceAnchor", toScreen, worldToPixels, points);
            if (!j->AutoConfigureDistance)
                drawList->AddCircle(toScreen(points.Other), j->Distance * worldToPixels, IM_COL32(120, 230, 255, 90), 48, 1.0f);
        }
        if (auto* j = SelectedComponent<SpringJoint2DComponent>(ctx, screen)) {
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##SpringAnchor", toScreen, worldToPixels, points);
            // Zig-zag between the anchors.
            const ImVec2 a = toScreen(points.Self), b = toScreen(points.Other);
            const glm::vec2 d{ b.x - a.x, b.y - a.y };
            const float length = glm::length(d);
            if (length > 8.0f) {
                const glm::vec2 u = d / length, n{ -u.y, u.x };
                ImVec2 previous = a;
                for (int i = 1; i <= 10; ++i) {
                    const float t = static_cast<float>(i) / 10.0f;
                    const float side = (i == 10) ? 0.0f : ((i % 2) ? 5.0f : -5.0f);
                    const ImVec2 p(a.x + d.x * t + n.x * side, a.y + d.y * t + n.y * side);
                    drawList->AddLine(previous, p, LimitColor, 1.5f);
                    previous = p;
                }
            }
        }
        if (auto* j = SelectedComponent<HingeJoint2DComponent>(ctx, screen)) {
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##HingeAnchor", toScreen, worldToPixels, points);
            if (j->UseLimits)
                Arc2D(drawList, toScreen(points.Self), 26.0f, world.Rotation.z + std::min(j->LowerAngle, j->UpperAngle),
                      world.Rotation.z + std::max(j->LowerAngle, j->UpperAngle), LimitColor);
        }
        if (auto* j = SelectedComponent<FixedJoint2DComponent>(ctx, screen))
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##FixedAnchor", toScreen, worldToPixels, points);
        if (auto* j = SelectedComponent<FrictionJoint2DComponent>(ctx, screen))
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##FrictionAnchor", toScreen, worldToPixels, points);
        if (auto* j = SelectedComponent<RelativeJoint2DComponent>(ctx, screen))
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##RelativeAnchor", toScreen, worldToPixels, points);
        if (auto* j = SelectedComponent<SliderJoint2DComponent>(ctx, screen)) {
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##SliderAnchor", toScreen, worldToPixels, points);
            const glm::vec2 axis = Direction2D(j->Angle);
            const float reach = j->UseLimits ? std::max(std::fabs(j->LowerTranslation), std::fabs(j->UpperTranslation)) : 60.0f / worldToPixels;
            if (j->UseLimits)
                drawList->AddLine(toScreen(points.Other + axis * j->LowerTranslation), toScreen(points.Other + axis * j->UpperTranslation), LimitColor, 3.0f);
            else
                drawList->AddLine(toScreen(points.Other - axis * reach), toScreen(points.Other + axis * reach), LimitColor, 1.0f);
        }
        if (auto* j = SelectedComponent<WheelJoint2DComponent>(ctx, screen)) {
            changed |= DrawJointAnchors2D(ctx, drawList, selected, *j, "##WheelAnchor", toScreen, worldToPixels, points);
            const glm::vec2 axis = Rotate2D(Direction2D(j->SuspensionAngle), world.Rotation.z);
            Arrow(drawList, toScreen(points.Self), toScreen(points.Self + axis * (40.0f / worldToPixels)), LimitColor);
        }
        if (auto* j = SelectedComponent<TargetJoint2DComponent>(ctx, screen)) {
            const glm::vec2 anchor = position + Rotate2D(j->Anchor, world.Rotation.z);
            const glm::vec2 target = j->AutoConfigureTarget ? anchor : j->Target;
            const ImVec2 anchorScreen = toScreen(anchor), targetScreen = toScreen(target);
            drawList->AddLine(anchorScreen, targetScreen, ConnectedColor, 1.5f);
            ImVec2 delta;
            if (DragHandle("##TargetJointAnchor", drawList, anchorScreen, AnchorColor, delta)) {
                j->Anchor += Rotate2D(glm::vec2(delta.x, delta.y) / worldToPixels, -world.Rotation.z);
                changed = true;
            }
            if (!j->AutoConfigureTarget && DragHandle("##TargetJointTarget", drawList, targetScreen, LimitColor, delta)) {
                j->Target += glm::vec2(delta.x, delta.y) / worldToPixels;
                changed = true;
            }
            drawList->AddLine(ImVec2(targetScreen.x - 8, targetScreen.y), ImVec2(targetScreen.x + 8, targetScreen.y), LimitColor, 2.0f);
            drawList->AddLine(ImVec2(targetScreen.x, targetScreen.y - 8), ImVec2(targetScreen.x, targetScreen.y + 8), LimitColor, 2.0f);
        }

        // Effectors: direction / shape hints like Unity's.
        const ImVec2 center = toScreen(position);
        const glm::vec2 half = ColliderHalfExtent2D(selected);
        if (auto* e = SelectedComponent<AreaEffector2DComponent>(ctx, screen)) {
            glm::vec2 direction = Direction2D(e->ForceAngle);
            if (!e->UseGlobalAngle)
                direction = Rotate2D(direction, world.Rotation.z);
            const float length = std::clamp(std::fabs(e->ForceMagnitude) * 6.0f, 20.0f, 70.0f) * (e->ForceMagnitude < 0.0f ? -1.0f : 1.0f);
            for (int i = -1; i <= 1; ++i) {
                const glm::vec2 side = glm::vec2(-direction.y, direction.x) * (half * 0.5f * static_cast<float>(i));
                const ImVec2 from = toScreen(position + side);
                Arrow(drawList, from, ImVec2(from.x + direction.x * length, from.y + direction.y * length), EffectorColor);
            }
        }
        if (auto* e = SelectedComponent<PointEffector2DComponent>(ctx, screen)) {
            const float sign = e->ForceMagnitude < 0.0f ? -1.0f : 1.0f;
            const float radius = std::max(half.x, half.y) * worldToPixels;
            for (int i = 0; i < 8; ++i) {
                const float a = glm::two_pi<float>() * i / 8.0f;
                const ImVec2 inner(center.x + std::cos(a) * radius * 0.35f, center.y + std::sin(a) * radius * 0.35f);
                const ImVec2 outer(center.x + std::cos(a) * radius * 0.85f, center.y + std::sin(a) * radius * 0.85f);
                Arrow(drawList, sign > 0.0f ? inner : outer, sign > 0.0f ? outer : inner, EffectorColor, 1.5f);
            }
        }
        if (auto* e = SelectedComponent<SurfaceEffector2DComponent>(ctx, screen)) {
            const glm::vec2 x = Rotate2D({ 1.0f, 0.0f }, world.Rotation.z) * (e->Speed < 0.0f ? -1.0f : 1.0f);
            const glm::vec2 top = position + Rotate2D({ 0.0f, -half.y }, world.Rotation.z);
            Arrow(drawList, toScreen(top - x * half.x * 0.6f), toScreen(top + x * half.x * 0.6f), EffectorColor);
        }
        if (auto* e = SelectedComponent<PlatformEffector2DComponent>(ctx, screen)) {
            // Up arrow plus the surface arc (contacts inside it collide).
            const float upDegrees = world.Rotation.z + e->RotationalOffset - 90.0f; // screen angle of -Y
            const float halfArc = std::clamp(e->SurfaceArc, 0.0f, 360.0f) * 0.5f;
            Arc2D(drawList, center, std::max(half.x, half.y) * worldToPixels + 12.0f, upDegrees - halfArc, upDegrees + halfArc, EffectorColor);
            const float r = glm::radians(upDegrees);
            Arrow(drawList, center, ImVec2(center.x + std::cos(r) * 36.0f, center.y + std::sin(r) * 36.0f), EffectorColor);
        }
        if (auto* e = SelectedComponent<BuoyancyEffector2DComponent>(ctx, screen)) {
            const float surfaceY = position.y - e->SurfaceLevel;
            drawList->AddLine(toScreen({ position.x - half.x, surfaceY }), toScreen({ position.x + half.x, surfaceY }), IM_COL32(80, 160, 255, 255), 3.0f);
            drawList->AddRectFilled(toScreen({ position.x - half.x, surfaceY }), toScreen({ position.x + half.x, position.y + half.y }), IM_COL32(80, 160, 255, 40));
        }
        if (auto* f = SelectedComponent<ConstantForce2DComponent>(ctx, screen)) {
            const glm::vec2 force = f->Force + Rotate2D(f->RelativeForce, world.Rotation.z);
            if (glm::length(force) > 1e-4f)
                Arrow(drawList, center, toScreen(position + glm::normalize(force) * (50.0f / worldToPixels)), EffectorColor);
        }

        if (changed)
            MarkSceneDirty(ctx);
    }

    void DrawComponentGizmos3D(EditorContext& ctx, Screen screen, ImDrawList* drawList,
                              const std::function<bool(const glm::vec3&, ImVec2&)>& project) {
        if (!ctx.Selected)
            return;
        Entity selected = ctx.Selected;
        const TransformComponent world = ctx.SceneRef.GetWorldTransform(selected);

        if (auto* cc = SelectedComponent<CharacterControllerComponent>(ctx, screen)) {
            // Upright capsule (Height includes the caps), Y axis; the engine's up is -Y.
            const glm::vec3 c = world.Translation + cc->Center;
            const float r = std::max(cc->Radius, 0.001f);
            const float halfCylinder = std::max(0.0f, cc->Height * 0.5f - r);
            const glm::vec3 up(0.0f, -1.0f, 0.0f), x(1, 0, 0), z(0, 0, 1);
            const glm::vec3 top = c + up * halfCylinder, bottom = c - up * halfCylinder;
            Circle3D(drawList, project, top, x, z, r, ControllerColor);
            Circle3D(drawList, project, bottom, x, z, r, ControllerColor);
            for (const glm::vec3& side : { x, -x, z, -z })
                Line3D(drawList, project, top + side * r, bottom + side * r, ControllerColor);
            Circle3D(drawList, project, top, x, up, r, ControllerColor, 0.0f, glm::pi<float>());
            Circle3D(drawList, project, top, z, up, r, ControllerColor, 0.0f, glm::pi<float>());
            Circle3D(drawList, project, bottom, x, -up, r, ControllerColor, 0.0f, glm::pi<float>());
            Circle3D(drawList, project, bottom, z, -up, r, ControllerColor, 0.0f, glm::pi<float>());
        }

        DrawJoint3D<FixedJointComponent>(ctx, screen, drawList, project, nullptr);
        DrawJoint3D<SpringJointComponent>(ctx, screen, drawList, project, nullptr);
        if (auto* hinge = SelectedComponent<HingeJointComponent>(ctx, screen)) {
            const glm::vec3 axis = hinge->Axis;
            DrawJoint3D<HingeJointComponent>(ctx, screen, drawList, project, &axis);
            if (hinge->UseLimits) {
                const glm::quat rotation = EulerToQuat(world.Rotation);
                const glm::vec3 a = glm::normalize(rotation * (glm::length(axis) > 1e-5f ? axis : glm::vec3(1, 0, 0)));
                glm::vec3 u = glm::cross(a, glm::vec3(0, 1, 0));
                if (glm::length(u) < 1e-3f) u = glm::cross(a, glm::vec3(1, 0, 0));
                u = glm::normalize(u);
                const glm::vec3 v = glm::cross(a, u);
                Circle3D(drawList, project, world.Translation + rotation * hinge->Anchor, u, v, 0.4f, LimitColor,
                         glm::radians(std::min(hinge->MinLimit, hinge->MaxLimit)), glm::radians(std::max(hinge->MinLimit, hinge->MaxLimit)));
            }
        }
        if (auto* character = SelectedComponent<CharacterJointComponent>(ctx, screen)) {
            const glm::vec3 axis = character->Axis;
            DrawJoint3D<CharacterJointComponent>(ctx, screen, drawList, project, &axis);
        }

        if (auto* mesh = SelectedComponent<MeshColliderComponent>(ctx, screen)) {
            // Same mesh the physics uses (Physics3DExtensions.cpp): empty = the Mesh Filter's.
            AssetRef meshRef = mesh->Mesh;
            const MeshFilterComponent* filter = selected.TryGetComponent<MeshFilterComponent>();
            if (meshRef.Guid.empty() && filter)
                meshRef = filter->Mesh;
            const std::vector<MeshVertex>* vertices = MeshReference::Vertices(meshRef);
            const glm::quat rotation = EulerToQuat(world.Rotation);
            const size_t triangles = vertices ? std::min<size_t>(vertices->size() / 3, 1500) : 0;
            for (size_t t = 0; t < triangles; ++t) {
                glm::vec3 p[3];
                for (int k = 0; k < 3; ++k)
                    p[k] = world.Translation + rotation * ((*vertices)[t * 3 + k].Position * world.Scale);
                Line3D(drawList, project, p[0], p[1], ColliderColor, 1.0f);
                Line3D(drawList, project, p[1], p[2], ColliderColor, 1.0f);
                Line3D(drawList, project, p[2], p[0], ColliderColor, 1.0f);
            }
        }

        if (auto* lod = SelectedComponent<LODGroupComponent>(ctx, screen)) {
            ImVec2 s;
            if (project(world.Translation, s)) {
                char label[48];
                if (lod->CurrentLOD >= 0)
                    std::snprintf(label, sizeof(label), "LOD %d", lod->CurrentLOD);
                else
                    std::snprintf(label, sizeof(label), lod->CurrentLOD == -1 ? "Culled" : "LOD -");
                drawList->AddRectFilled(ImVec2(s.x + 8, s.y - 18), ImVec2(s.x + 70, s.y - 2), IM_COL32(20, 20, 20, 190), 3.0f);
                drawList->AddText(ImVec2(s.x + 12, s.y - 17), IM_COL32(255, 220, 120, 255), label);
            }
        }
    }

}
