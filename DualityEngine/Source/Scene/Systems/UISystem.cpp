// UI layout and widget input beyond the basic Canvas widgets: Grid Layout Group, Content Size
// Fitter, Aspect Ratio Fitter, Scroll Rect / Scrollbar / Dropdown, and the UI Mask / Canvas
// Group queries the UI renderer and input share (Scene/SceneRuntimeSystems.h).
#include "DualityEngine/Scene/SceneRuntimeSystems.h"

#include <algorithm>
#include <cmath>

#include "DualityEngine/Input/InputManager.h"
#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace {
        Entity RefEntity(Scene& scene, const EntityRef& ref) {
            if (ref.Handle == EntityRef::Invalid)
                return {};
            Entity entity(static_cast<entt::entity>(ref.Handle), &scene);
            return entity.IsValid() ? entity : Entity{};
        }

        // -------------------------------------------------------------------- UI helpers
        struct Rect { glm::vec2 Min, Max; };

        Rect ResolveRect(Scene& scene, Entity entity) {
            glm::vec2 topLeft, size;
            ResolveUIRect(scene, entity, topLeft, size);
            return { topLeft, topLeft + size };
        }

        Entity ParentOf(Entity entity) {
            return entity.GetComponent<HierarchyComponent>().Parent;
        }

        bool UsableRect(Scene& scene, Entity entity) {
            return entity && entity.HasComponent<UIRectComponent>() && entity.GetComponent<UIRectComponent>().Enabled &&
                scene.IsEffectivelyActive(entity);
        }

        // Preferred size of a UI element for Content Size Fitter (pixels).
        glm::vec2 PreferredSize(Scene& scene, Entity entity) {
            glm::vec2 size = entity.GetComponent<UIRectComponent>().SizeDelta;
            if (auto* text = entity.TryGetComponent<UITextComponent>())
                return MeasureUITextForLayout(*text);
            const auto& children = entity.GetComponent<HierarchyComponent>().Children;
            if (auto* grid = entity.TryGetComponent<UIGridLayoutGroupComponent>(); grid && grid->Enabled) {
                int count = 0;
                for (Entity child : children)
                    if (UsableRect(scene, child)) ++count;
                const float width = std::max(1.0f, size.x - grid->Padding.x - grid->Padding.y);
                int columns = 1;
                if (grid->Constraint == GridLayoutConstraint::FixedColumnCount)
                    columns = std::max(1, grid->ConstraintCount);
                else if (grid->Constraint == GridLayoutConstraint::FixedRowCount)
                    columns = std::max(1, (count + std::max(1, grid->ConstraintCount) - 1) / std::max(1, grid->ConstraintCount));
                else
                    columns = std::max(1, static_cast<int>((width + grid->Spacing.x) / std::max(1.0f, grid->CellSize.x + grid->Spacing.x)));
                const int rows = std::max(1, (count + columns - 1) / columns);
                return { grid->Padding.x + grid->Padding.y + columns * grid->CellSize.x + (columns - 1) * grid->Spacing.x,
                         grid->Padding.z + grid->Padding.w + rows * grid->CellSize.y + (rows - 1) * grid->Spacing.y };
            }
            if (auto* layout = entity.TryGetComponent<UILayoutGroupComponent>(); layout && layout->Enabled) {
                glm::vec2 sum(0.0f), maxSize(0.0f);
                int count = 0;
                for (Entity child : children) {
                    if (!UsableRect(scene, child))
                        continue;
                    const glm::vec2 childSize = child.GetComponent<UIRectComponent>().SizeDelta;
                    sum += childSize;
                    maxSize = glm::max(maxSize, childSize);
                    ++count;
                }
                const float spacing = layout->Spacing * std::max(0, count - 1);
                if (layout->Layout == UILayoutType::Vertical)
                    return { maxSize.x + layout->Padding.x * 2.0f, sum.y + spacing + layout->Padding.y * 2.0f };
                return { sum.x + spacing + layout->Padding.x * 2.0f, maxSize.y + layout->Padding.y * 2.0f };
            }
            return size;
        }

        void ApplyGridLayout(Scene& scene, Entity entity, UIGridLayoutGroupComponent& grid) {
            const Rect parent = ResolveRect(scene, entity);
            const glm::vec2 parentSize = parent.Max - parent.Min;
            const auto& children = entity.GetComponent<HierarchyComponent>().Children;
            int count = 0;
            for (Entity child : children)
                if (UsableRect(scene, child)) ++count;
            if (count == 0)
                return;
            const glm::vec2 cell = glm::max(grid.CellSize, glm::vec2(1.0f));
            const float innerWidth = std::max(1.0f, parentSize.x - grid.Padding.x - grid.Padding.y);
            const float innerHeight = std::max(1.0f, parentSize.y - grid.Padding.z - grid.Padding.w);
            int columns, rows;
            if (grid.Constraint == GridLayoutConstraint::FixedColumnCount) {
                columns = std::max(1, grid.ConstraintCount);
                rows = (count + columns - 1) / columns;
            } else if (grid.Constraint == GridLayoutConstraint::FixedRowCount) {
                rows = std::max(1, grid.ConstraintCount);
                columns = (count + rows - 1) / rows;
            } else if (grid.StartAxis == GridLayoutAxis::Horizontal) {
                columns = std::max(1, static_cast<int>((innerWidth + grid.Spacing.x) / (cell.x + grid.Spacing.x)));
                rows = (count + columns - 1) / columns;
            } else {
                rows = std::max(1, static_cast<int>((innerHeight + grid.Spacing.y) / (cell.y + grid.Spacing.y)));
                columns = (count + rows - 1) / rows;
            }
            const bool right = grid.StartCorner == GridLayoutCorner::UpperRight || grid.StartCorner == GridLayoutCorner::LowerRight;
            const bool bottom = grid.StartCorner == GridLayoutCorner::LowerLeft || grid.StartCorner == GridLayoutCorner::LowerRight;
            int index = 0;
            for (Entity child : children) {
                if (!UsableRect(scene, child))
                    continue;
                int column, row;
                if (grid.StartAxis == GridLayoutAxis::Horizontal) {
                    column = index % columns;
                    row = index / columns;
                } else {
                    row = index % rows;
                    column = index / rows;
                }
                ++index;
                if (right) column = columns - 1 - column;
                if (bottom) row = rows - 1 - row;
                auto& rect = child.GetComponent<UIRectComponent>();
                // Same full-control convention as UILayoutGroup: top-left anchored children.
                rect.AnchorMin = rect.AnchorMax = rect.Pivot = { 0.0f, 0.0f };
                rect.SizeDelta = cell;
                rect.AnchoredPosition = { grid.Padding.x + column * (cell.x + grid.Spacing.x),
                                          grid.Padding.z + row * (cell.y + grid.Spacing.y) };
            }
        }

        void ApplyAspectRatio(Scene& scene, Entity entity, const UIAspectRatioFitterComponent& fitter) {
            auto& rect = entity.GetComponent<UIRectComponent>();
            const float ratio = std::max(fitter.AspectRatio, 0.001f);
            switch (fitter.AspectMode) {
                case AspectRatioMode::WidthControlsHeight: {
                    const Rect current = ResolveRect(scene, entity);
                    const float height = (current.Max.x - current.Min.x) / ratio;
                    if (rect.AnchorMin.y == rect.AnchorMax.y)
                        rect.SizeDelta.y = height;
                    break;
                }
                case AspectRatioMode::HeightControlsWidth: {
                    const Rect current = ResolveRect(scene, entity);
                    const float width = (current.Max.y - current.Min.y) * ratio;
                    if (rect.AnchorMin.x == rect.AnchorMax.x)
                        rect.SizeDelta.x = width;
                    break;
                }
                case AspectRatioMode::FitInParent:
                case AspectRatioMode::EnvelopeParent: {
                    Entity parent = ParentOf(entity);
                    glm::vec2 parentSize;
                    if (parent && parent.HasComponent<UIRectComponent>()) {
                        const Rect p = ResolveRect(scene, parent);
                        parentSize = p.Max - p.Min;
                    } else {
                        // Canvas root: the physical screen.
                        Entity canvas = FindOwningCanvas(scene, entity);
                        const bool top = !canvas || canvas.GetComponent<CanvasComponent>().Screen == Screen::Top;
                        parentSize = { static_cast<float>(top ? TopScreenWidth : BottomScreenWidth),
                                       static_cast<float>(top ? TopScreenHeight : BottomScreenHeight) };
                    }
                    glm::vec2 size(parentSize.x, parentSize.x / ratio);
                    const bool fit = fitter.AspectMode == AspectRatioMode::FitInParent;
                    if ((fit && size.y > parentSize.y) || (!fit && size.y < parentSize.y))
                        size = { parentSize.y * ratio, parentSize.y };
                    rect.AnchorMin = { 0.0f, 0.0f };
                    rect.AnchorMax = { 1.0f, 1.0f };
                    rect.AnchoredPosition = { 0.0f, 0.0f };
                    rect.SizeDelta = size - parentSize;
                    break;
                }
                default:
                    break;
            }
        }

        // Moves Content so it stays inside Viewport; returns the correction needed (pixels).
        glm::vec2 ScrollBoundsOffset(const Rect& view, const Rect& content, const UIScrollRectComponent& scroll) {
            glm::vec2 offset(0.0f);
            for (int axis = 0; axis < 2; ++axis) {
                if ((axis == 0 && !scroll.Horizontal) || (axis == 1 && !scroll.Vertical))
                    continue;
                const float viewSize = view.Max[axis] - view.Min[axis];
                const float contentSize = content.Max[axis] - content.Min[axis];
                if (contentSize <= viewSize)
                    offset[axis] = view.Min[axis] - content.Min[axis]; // align to the top / left
                else if (content.Min[axis] > view.Min[axis])
                    offset[axis] = view.Min[axis] - content.Min[axis];
                else if (content.Max[axis] < view.Max[axis])
                    offset[axis] = view.Max[axis] - content.Max[axis];
            }
            return offset;
        }

        Entity ScrollViewport(Scene& scene, Entity entity, const UIScrollRectComponent& scroll) {
            Entity viewport = RefEntity(scene, scroll.Viewport);
            return viewport && viewport.HasComponent<UIRectComponent>() ? viewport : entity;
        }

        // Scrollbar track axis (0 = x, 1 = y) and whether value grows towards the min edge.
        int ScrollbarAxis(const UIScrollbarComponent& bar) {
            return bar.Direction == ScrollbarDirection::LeftToRight || bar.Direction == ScrollbarDirection::RightToLeft ? 0 : 1;
        }
        bool ScrollbarReversed(const UIScrollbarComponent& bar) {
            // Screen space is Y-down: "Bottom To Top" means value 1 at the top (min y).
            return bar.Direction == ScrollbarDirection::RightToLeft || bar.Direction == ScrollbarDirection::BottomToTop;
        }

        void SyncScrollbars(Scene& scene, UIScrollRectComponent& scroll, const Rect& view, const Rect& content) {
            for (int axis = 0; axis < 2; ++axis) {
                Entity barEntity = RefEntity(scene, axis == 0 ? scroll.HorizontalScrollbar : scroll.VerticalScrollbar);
                if (!barEntity || !barEntity.HasComponent<UIScrollbarComponent>())
                    continue;
                auto& bar = barEntity.GetComponent<UIScrollbarComponent>();
                const float viewSize = view.Max[axis] - view.Min[axis];
                const float contentSize = std::max(content.Max[axis] - content.Min[axis], 1.0f);
                bar.Size = std::clamp(viewSize / contentSize, 0.0f, 1.0f);
                if (bar.IsDragging)
                    continue; // the scrollbar drives the content instead (see the input pass)
                const float range = contentSize - viewSize;
                const float t = range > 0.0f ? std::clamp((view.Min[axis] - content.Min[axis]) / range, 0.0f, 1.0f) : 0.0f;
                // Vertical Unity convention: value 1 = top. Screen is Y-down, so invert.
                bar.Value = axis == 1 ? 1.0f - t : t;
            }
            const float rangeX = (content.Max.x - content.Min.x) - (view.Max.x - view.Min.x);
            const float rangeY = (content.Max.y - content.Min.y) - (view.Max.y - view.Min.y);
            scroll.NormalizedPosition = {
                rangeX > 0.0f ? std::clamp((view.Min.x - content.Min.x) / rangeX, 0.0f, 1.0f) : 0.0f,
                rangeY > 0.0f ? 1.0f - std::clamp((view.Min.y - content.Min.y) / rangeY, 0.0f, 1.0f) : 1.0f,
            };
        }

        float s_LastUIDeltaTime = 1.0f / 60.0f;
    }

    void UpdateUILayoutSystems(Scene& scene, float deltaTime) {
        auto& registry = scene.Registry();
        s_LastUIDeltaTime = deltaTime > 0.0f ? deltaTime : s_LastUIDeltaTime;

        for (auto handle : registry.view<UIGridLayoutGroupComponent, UIRectComponent, HierarchyComponent>()) {
            Entity entity(handle, &scene);
            auto& grid = registry.get<UIGridLayoutGroupComponent>(handle);
            if (grid.Enabled && scene.IsEffectivelyActive(entity))
                ApplyGridLayout(scene, entity, grid);
        }
        for (auto handle : registry.view<UIContentSizeFitterComponent, UIRectComponent>()) {
            Entity entity(handle, &scene);
            auto& fitter = registry.get<UIContentSizeFitterComponent>(handle);
            if (!fitter.Enabled || !scene.IsEffectivelyActive(entity))
                continue;
            auto& rect = registry.get<UIRectComponent>(handle);
            const glm::vec2 preferred = PreferredSize(scene, entity);
            if (fitter.HorizontalFit != ContentSizeFitMode::Unconstrained && rect.AnchorMin.x == rect.AnchorMax.x)
                rect.SizeDelta.x = preferred.x;
            if (fitter.VerticalFit != ContentSizeFitMode::Unconstrained && rect.AnchorMin.y == rect.AnchorMax.y)
                rect.SizeDelta.y = preferred.y;
        }
        for (auto handle : registry.view<UIAspectRatioFitterComponent, UIRectComponent>()) {
            Entity entity(handle, &scene);
            auto& fitter = registry.get<UIAspectRatioFitterComponent>(handle);
            if (fitter.Enabled && scene.IsEffectivelyActive(entity))
                ApplyAspectRatio(scene, entity, fitter);
        }

        // Scroll Rect inertia / elasticity (the pointer drag itself is in the input pass).
        const float dt = std::max(deltaTime, 0.0f);
        for (auto handle : registry.view<UIScrollRectComponent, UIRectComponent>()) {
            Entity entity(handle, &scene);
            auto& scroll = registry.get<UIScrollRectComponent>(handle);
            Entity content = RefEntity(scene, scroll.Content);
            if (!scroll.Enabled || !content || !content.HasComponent<UIRectComponent>() || !scene.IsEffectivelyActive(entity))
                continue;
            auto& contentRect = content.GetComponent<UIRectComponent>();
            const Rect view = ResolveRect(scene, ScrollViewport(scene, entity, scroll));
            if (!scroll.Dragging) {
                if (scroll.Inertia && glm::dot(scroll.Velocity, scroll.Velocity) > 0.01f) {
                    glm::vec2 move = scroll.Velocity * dt;
                    if (!scroll.Horizontal) move.x = 0.0f;
                    if (!scroll.Vertical) move.y = 0.0f;
                    contentRect.AnchoredPosition += move;
                    scroll.Velocity *= std::pow(std::clamp(scroll.DecelerationRate, 0.0f, 1.0f), dt);
                } else {
                    scroll.Velocity = glm::vec2(0.0f);
                }
                const glm::vec2 offset = ScrollBoundsOffset(view, ResolveRect(scene, content), scroll);
                if (offset != glm::vec2(0.0f)) {
                    if (scroll.MovementType == ScrollRectMovementType::Clamped) {
                        contentRect.AnchoredPosition += offset;
                        scroll.Velocity = glm::vec2(0.0f);
                    } else if (scroll.MovementType == ScrollRectMovementType::Elastic) {
                        // Critically-damped return, Elasticity ~ the time it takes.
                        const float k = 1.0f - std::exp(-dt / std::max(scroll.Elasticity * 0.5f, 0.01f));
                        glm::vec2 step = offset * k;
                        if (std::fabs(offset.x) < 0.5f) step.x = offset.x;
                        if (std::fabs(offset.y) < 0.5f) step.y = offset.y;
                        contentRect.AnchoredPosition += step;
                        for (int axis = 0; axis < 2; ++axis)
                            if (offset[axis] != 0.0f) scroll.Velocity[axis] = 0.0f;
                    }
                }
            }
            SyncScrollbars(scene, scroll, view, ResolveRect(scene, content));
        }
    }

    void UpdateUIWidgetInteractions(Scene& scene) {
        auto& registry = scene.Registry();
        const glm::vec2 pointer = InputManager::GetPointerPosition();
        const bool pointerDown = InputManager::GetPointerDown();
        const bool pointerPressed = InputManager::GetPointerPressed();
        const bool pointerUp = InputManager::GetPointerUp();
        const Screen pointerScreen = InputManager::GetPointerScreen();
        auto inside = [&](Entity entity, const Rect& rect) {
            return IsUIElementOnCanvas(scene, entity, pointerScreen) &&
                pointer.x >= rect.Min.x && pointer.x <= rect.Max.x && pointer.y >= rect.Min.y && pointer.y <= rect.Max.y;
        };

        // Dropdowns first: an open list captures the click.
        bool clickConsumed = false;
        for (auto handle : registry.view<UIDropdownComponent, UIRectComponent>()) {
            Entity entity(handle, &scene);
            auto& dropdown = registry.get<UIDropdownComponent>(handle);
            dropdown.WasChanged = false;
            if (!dropdown.Enabled || !scene.IsEffectivelyActive(entity) || !registry.get<UIRectComponent>(handle).Enabled) {
                dropdown.IsExpanded = false;
                continue;
            }
            const Rect caption = ResolveRect(scene, entity);
            const int optionCount = 1 + static_cast<int>(std::count(dropdown.Options.begin(), dropdown.Options.end(), '|'));
            dropdown.HoveredItem = -1;
            if (dropdown.IsExpanded) {
                const float itemHeight = std::max(dropdown.ItemHeight, 4.0f);
                // Same placement rule as the renderer: below the caption unless it would leave
                // the screen, then above.
                Entity canvas = FindOwningCanvas(scene, entity);
                const float screenHeight = static_cast<float>(!canvas || canvas.GetComponent<CanvasComponent>().Screen == Screen::Top ? TopScreenHeight : BottomScreenHeight);
                float listTop = caption.Max.y;
                if (listTop + itemHeight * optionCount > screenHeight)
                    listTop = caption.Min.y - itemHeight * optionCount;
                const Rect list{ { caption.Min.x, listTop }, { caption.Max.x, listTop + itemHeight * optionCount } };
                if (inside(entity, list))
                    dropdown.HoveredItem = std::clamp(static_cast<int>((pointer.y - listTop) / itemHeight), 0, optionCount - 1);
                if (pointerPressed && !clickConsumed && IsUIPointerAllowed(scene, entity, pointer)) {
                    if (dropdown.HoveredItem >= 0) {
                        if (dropdown.HoveredItem != dropdown.Value)
                            dropdown.WasChanged = true;
                        dropdown.Value = dropdown.HoveredItem;
                    }
                    dropdown.IsExpanded = false;
                    clickConsumed = true;
                }
            } else if (pointerPressed && !clickConsumed && inside(entity, caption) && IsUIPointerAllowed(scene, entity, pointer)) {
                dropdown.IsExpanded = true;
                clickConsumed = true;
            }
            dropdown.Value = std::clamp(dropdown.Value, 0, optionCount - 1);
        }

        // Scrollbars: drag the handle (or click the track to jump).
        for (auto handle : registry.view<UIScrollbarComponent, UIRectComponent>()) {
            Entity entity(handle, &scene);
            auto& bar = registry.get<UIScrollbarComponent>(handle);
            bar.WasChanged = false;
            if (!bar.Enabled || !scene.IsEffectivelyActive(entity)) {
                bar.IsDragging = false;
                continue;
            }
            const Rect track = ResolveRect(scene, entity);
            const int axis = ScrollbarAxis(bar);
            const float trackLength = std::max(track.Max[axis] - track.Min[axis], 1.0f);
            const float handleLength = trackLength * std::clamp(bar.Size, 0.05f, 1.0f);
            if (pointerPressed && !clickConsumed && inside(entity, track) && IsUIPointerAllowed(scene, entity, pointer)) {
                bar.IsDragging = true;
                clickConsumed = true;
            }
            if (bar.IsDragging && pointerDown) {
                float t = (pointer[axis] - track.Min[axis] - handleLength * 0.5f) / std::max(trackLength - handleLength, 1.0f);
                t = std::clamp(t, 0.0f, 1.0f);
                if (ScrollbarReversed(bar))
                    t = 1.0f - t;
                if (bar.NumberOfSteps > 1)
                    t = std::round(t * (bar.NumberOfSteps - 1)) / static_cast<float>(bar.NumberOfSteps - 1);
                if (t != bar.Value)
                    bar.WasChanged = true;
                bar.Value = t;
            } else if (!pointerDown) {
                bar.IsDragging = false;
            }
        }

        // Scroll Rects: drag the content.
        for (auto handle : registry.view<UIScrollRectComponent, UIRectComponent>()) {
            Entity entity(handle, &scene);
            auto& scroll = registry.get<UIScrollRectComponent>(handle);
            Entity content = RefEntity(scene, scroll.Content);
            if (!scroll.Enabled || !content || !content.HasComponent<UIRectComponent>() || !scene.IsEffectivelyActive(entity)) {
                scroll.Dragging = false;
                continue;
            }
            auto& contentRect = content.GetComponent<UIRectComponent>();
            const Rect view = ResolveRect(scene, ScrollViewport(scene, entity, scroll));

            // A dragged scrollbar moves the content.
            for (int axis = 0; axis < 2; ++axis) {
                Entity barEntity = RefEntity(scene, axis == 0 ? scroll.HorizontalScrollbar : scroll.VerticalScrollbar);
                if (!barEntity || !barEntity.HasComponent<UIScrollbarComponent>())
                    continue;
                const auto& bar = barEntity.GetComponent<UIScrollbarComponent>();
                if (!bar.IsDragging)
                    continue;
                const Rect contentNow = ResolveRect(scene, content);
                const float range = (contentNow.Max[axis] - contentNow.Min[axis]) - (view.Max[axis] - view.Min[axis]);
                if (range <= 0.0f)
                    continue;
                const float t = axis == 1 ? 1.0f - bar.Value : bar.Value;
                const float desiredMin = view.Min[axis] - t * range;
                contentRect.AnchoredPosition[axis] += desiredMin - contentNow.Min[axis];
                scroll.Velocity[axis] = 0.0f;
            }

            if (!scroll.Dragging && pointerPressed && !clickConsumed && inside(entity, view) && IsUIPointerAllowed(scene, entity, pointer)) {
                scroll.Dragging = true;
                scroll.LastPointer = pointer;
                scroll.Velocity = glm::vec2(0.0f);
            }
            if (scroll.Dragging) {
                if (!pointerDown || pointerUp) {
                    scroll.Dragging = false;
                } else {
                    glm::vec2 delta = (pointer - scroll.LastPointer) * std::max(scroll.ScrollSensitivity, 0.0f);
                    if (!scroll.Horizontal) delta.x = 0.0f;
                    if (!scroll.Vertical) delta.y = 0.0f;
                    if (scroll.MovementType == ScrollRectMovementType::Elastic) {
                        // Rubber band: dragging past the edge moves the content less.
                        const glm::vec2 offset = ScrollBoundsOffset(view, ResolveRect(scene, content), scroll);
                        for (int axis = 0; axis < 2; ++axis)
                            if (offset[axis] != 0.0f && delta[axis] * offset[axis] < 0.0f)
                                delta[axis] *= 0.35f;
                    }
                    contentRect.AnchoredPosition += delta;
                    if (scroll.MovementType == ScrollRectMovementType::Clamped)
                        contentRect.AnchoredPosition += ScrollBoundsOffset(view, ResolveRect(scene, content), scroll);
                    const glm::vec2 velocity = delta / std::max(s_LastUIDeltaTime, 1.0f / 240.0f);
                    scroll.Velocity = glm::mix(scroll.Velocity, velocity, 0.5f);
                    scroll.LastPointer = pointer;
                }
            }
        }
    }

    bool ResolveUIClipRect(Scene& scene, Entity entity, glm::vec4& outRect, bool includeSelf) {
        bool clipped = false;
        glm::vec2 clipMin(-1e9f), clipMax(1e9f);
        for (Entity current = includeSelf ? entity : ParentOf(entity); current; current = ParentOf(current)) {
            if (!current.HasComponent<UIRectComponent>())
                break;
            const auto* mask = current.TryGetComponent<UIMaskComponent>();
            const auto* rectMask = current.TryGetComponent<UIRectMask2DComponent>();
            const bool masks = (mask && mask->Enabled) || (rectMask && rectMask->Enabled);
            if (!masks)
                continue;
            Rect rect = ResolveRect(scene, current);
            if (rectMask && rectMask->Enabled) {
                rect.Min += glm::vec2(rectMask->Padding.x, rectMask->Padding.y);
                rect.Max -= glm::vec2(rectMask->Padding.z, rectMask->Padding.w);
            }
            clipMin = glm::max(clipMin, rect.Min);
            clipMax = glm::min(clipMax, rect.Max);
            clipped = true;
        }
        if (!clipped)
            return false;
        const glm::vec2 size = glm::max(clipMax - clipMin, glm::vec2(0.0f));
        outRect = { clipMin.x, clipMin.y, size.x, size.y };
        return true;
    }

    float ResolveCanvasGroupAlpha(Scene& scene, Entity entity) {
        (void)scene;
        float alpha = 1.0f;
        for (Entity current = entity; current; current = ParentOf(current)) {
            if (const auto* group = current.TryGetComponent<UICanvasGroupComponent>(); group && group->Enabled) {
                alpha *= std::clamp(group->Alpha, 0.0f, 1.0f);
                if (group->IgnoreParentGroups)
                    break;
            }
        }
        return alpha;
    }

    bool IsUIPointerAllowed(Scene& scene, Entity entity, const glm::vec2& pointer) {
        for (Entity current = entity; current; current = ParentOf(current)) {
            if (const auto* group = current.TryGetComponent<UICanvasGroupComponent>(); group && group->Enabled) {
                if (!group->Interactable || !group->BlocksRaycasts)
                    return false;
                if (group->IgnoreParentGroups)
                    break;
            }
        }
        glm::vec4 clip;
        if (ResolveUIClipRect(scene, entity, clip))
            return pointer.x >= clip.x && pointer.x <= clip.x + clip.z && pointer.y >= clip.y && pointer.y <= clip.y + clip.w;
        return true;
    }
}
