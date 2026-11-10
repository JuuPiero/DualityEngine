#pragma once

// Canvas UI components (screen-space, drawn by Renderer/UIRenderer.h): Canvas, UI Rect and every
// widget / layout / effect component. Part of Scene/Components.h.

#include <string>

#include <glm/glm.hpp>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Renderer/CanvasRenderMode.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Renderer/TextAlignment.h"
#include "DualityEngine/Renderer/UIAnchor.h"
#include "DualityEngine/Renderer/UILayoutType.h"

namespace Duality {

    enum class ScrollRectMovementType { Unrestricted, Elastic, Clamped };
    enum class ScrollbarDirection { LeftToRight, RightToLeft, BottomToTop, TopToBottom };
    enum class ContentSizeFitMode { Unconstrained, MinSize, PreferredSize };
    enum class AspectRatioMode { None, WidthControlsHeight, HeightControlsWidth, FitInParent, EnvelopeParent };
    enum class GridLayoutCorner { UpperLeft, UpperRight, LowerLeft, LowerRight };
    enum class GridLayoutAxis { Horizontal, Vertical };
    enum class GridLayoutConstraint { Flexible, FixedColumnCount, FixedRowCount };

    // --- UI (screen-space overlay, drawn last/on top every frame -- see Renderer/UIRenderer.h)
    // ---
    //
    // Deliberately separate from TransformComponent/world-space rendering: a UI element lives
    // in a fixed physical screen's own pixel space, with no camera involved at all (no pan/zoom
    // relative to gameplay), matching Unity's Canvas "Screen Space - Overlay" render mode --
    // the only mode this engine supports (no "Screen Space - Camera"/"World Space" canvas modes
    // yet). Every UI widget type follows the same two-component split as this first one
    // (UIRectComponent for where/how big, a second component for what it looks like/does) --
    // adding a new widget type later (e.g. a slider) means adding one new component + one loop
    // in UIRenderer.cpp, without touching this positioning component at all.

    // Where/how big a UI element is -- Unity RectTransform-equivalent. AnchorMin/AnchorMax are
    // normalized [0,1] positions within the parent rect (or the physical screen, at the root);
    // equal on an axis means a point anchor, different means that axis stretches with the
    // parent. Pivot is a normalized [0,1] point within this rect's own resolved bounds.
    // AnchoredPosition offsets the pivot from the anchor point (point-anchor axes) or shifts a
    // stretched rect from the parent's own center (stretch axes; ignored on that axis
    // otherwise -- see Renderer/UIRenderer.h's ResolveUIRect). SizeDelta is this rect's size on
    // a point-anchor axis, or the amount added to the anchor-implied size on a stretch axis.
    // UIAnchor (Renderer/UIAnchor.h) is kept only as an editor-side "anchor preset" convenience
    // for quick-setting AnchorMin/AnchorMax/Pivot -- it is not stored on the component itself.
    struct UIRectComponent {
        bool Enabled = true;
        // Legacy load hint only. It is no longer reflected or saved; the owning Canvas chooses
        // the screen for all new Canvas-first UI.
        Duality::Screen Screen = Duality::Screen::Top;
        glm::vec2 AnchorMin{ 0.5f, 0.5f };
        glm::vec2 AnchorMax{ 0.5f, 0.5f };
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec2 AnchoredPosition{ 0.0f, 0.0f };
        glm::vec2 SizeDelta{ 100.0f, 40.0f };
        int SortOrder = 0;
    };

    // The visual half of a basic Panel/Image widget (pair with UIRectComponent). Same
    // Color/Texture convention as SpriteRendererComponent (empty Texture Guid = flat Color).
    // If the same entity also has a UIButtonComponent, that component's own
    // Normal/Hover/Pressed color takes over instead of this Color -- see UIButtonComponent.
    struct UIImageComponent {
        bool Enabled = true;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        AssetRef Texture;
        glm::vec4 SliceBorder{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // Makes a (UIRectComponent, UIImageComponent) pair clickable -- pair all three on one
    // entity for a basic Button. IsHovered/IsPressed/WasClicked are updated once per frame by
    // UIRenderer.cpp's UpdateUIInteractions (called once per frame by the app entry point,
    // before Scene::OnRuntimeUpdate so scripts see this frame's state) and are meant to be
    // polled from a script the same way GetKeyDown() is: `GetComponent<UIButtonComponent>().
    // WasClicked`. Not reflected as authored-editable at Play time (IsHovered/IsPressed/
    // WasClicked are runtime state, like SpriteFlipbookComponent::CurrentFrame) -- only the
    // three colors are.
    struct UIButtonComponent {
        bool Enabled = true;
        glm::vec4 NormalColor{ 0.85f, 0.85f, 0.85f, 1.0f };
        glm::vec4 HoverColor{ 0.95f, 0.95f, 0.6f, 1.0f };
        glm::vec4 PressedColor{ 0.7f, 0.7f, 0.4f, 1.0f };

        bool IsHovered = false;
        bool IsPressed = false;
        bool WasClicked = false; // true for exactly one frame: pointer released while still over the button
    };

    // A text label (pair with UIRectComponent under a Canvas). Drawn by
    // UIRenderer.cpp via IRenderer2D::DrawText, horizontally positioned within the paired
    // UIRectComponent's resolved rect per Alignment -- vertical is always centered (a
    // deliberate v1 scope limit, no separate vertical-alignment enum). Font is an AssetRef to a
    // .ttf/.otf (empty = default/system font); real per-platform behavior differs -- see
    // IRenderer2D::LoadFont's own comment (desktop rasterizes any assigned font for real,
    // 3DS always uses citro2d's built-in system font regardless, logged once if ignored).
    struct UITextComponent {
        bool Enabled = true;
        std::string Text;
        AssetRef Font;
        float FontSize = 16.0f;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        TextAlignment Alignment = TextAlignment::Left;
    };

    // Unity Canvas root. Every UI Rect belongs beneath one Canvas; it owns the physical 3DS
    // screen, render mode and sort order instead of every child carrying those concerns.
    struct CanvasComponent {
        bool Enabled = true;
        Duality::Screen Screen = Duality::Screen::Top;
        CanvasRenderMode RenderMode = CanvasRenderMode::ScreenSpaceOverlay;
        int SortOrder = 0;
        float ScaleFactor = 1.0f;
        EntityRef TargetCamera; // for ScreenSpaceCamera
    };

    struct UILayoutGroupComponent {
        bool Enabled = true;
        UILayoutType Layout = UILayoutType::Vertical;
        float Spacing = 4.0f;
        glm::vec2 Padding{ 4.0f, 4.0f };
        bool ChildControlWidth = true;
        bool ChildControlHeight = true;
    };

    struct UISliderComponent {
        bool Enabled = true;
        float Value = 0.5f;
        float MinValue = 0.0f;
        float MaxValue = 1.0f;
        bool IsHovered = false;
        bool IsDragging = false;
    };

    struct UIToggleComponent {
        bool Enabled = true;
        bool IsOn = false;
        bool IsHovered = false;
        bool WasToggled = false;
    };

    struct UIInputFieldComponent {
        bool Enabled = true;
        std::string Text;
        std::string Placeholder = "Enter text...";
        bool IsFocused = false;
        bool IsHovered = false;
    };

    // Drag (touch / mouse) the Content rect inside the Viewport rect (this entity when empty).
    // Elastic overscroll springs back, Inertia keeps scrolling after release.
    struct UIScrollRectComponent {
        bool Enabled = true;
        EntityRef Content;
        bool Horizontal = true;
        bool Vertical = true;
        ScrollRectMovementType MovementType = ScrollRectMovementType::Elastic;
        float Elasticity = 0.1f;
        bool Inertia = true;
        float DecelerationRate = 0.135f;
        float ScrollSensitivity = 1.0f;
        EntityRef Viewport;
        EntityRef HorizontalScrollbar;
        EntityRef VerticalScrollbar;

        // Runtime.
        glm::vec2 Velocity{ 0.0f };
        glm::vec2 NormalizedPosition{ 0.0f, 1.0f }; // Unity: (0,1) = top-left
        bool Dragging = false;
        glm::vec2 LastPointer{ 0.0f };
    };

    struct UIScrollbarComponent {
        bool Enabled = true;
        ScrollbarDirection Direction = ScrollbarDirection::LeftToRight;
        float Value = 0.0f;
        float Size = 0.2f;
        int NumberOfSteps = 0;
        glm::vec4 HandleColor{ 0.85f, 0.85f, 0.85f, 1.0f };
        // Runtime.
        bool IsDragging = false;
        bool WasChanged = false;
        float DragOffset = 0.0f;
    };

    // Clips every UI descendant to this rect. Rectangular on both platforms (GPU scissor test:
    // glScissor / C3D_SetScissor) -- a non-rectangular sprite stencil is not supported.
    struct UIMaskComponent {
        bool Enabled = true;
        bool ShowMaskGraphic = true;
    };

    struct UIRectMask2DComponent {
        bool Enabled = true;
        glm::vec4 Padding{ 0.0f, 0.0f, 0.0f, 0.0f }; // left, top, right, bottom (pixels, inwards)
    };

    // Options are separated by '|'. Clicking the caption opens the list (drawn on top of the
    // whole canvas); WasChanged is true for one frame after a different option is picked.
    struct UIDropdownComponent {
        bool Enabled = true;
        std::string Options = "Option A|Option B|Option C";
        int Value = 0;
        AssetRef Font;
        float FontSize = 14.0f;
        glm::vec4 TextColor{ 0.1f, 0.1f, 0.1f, 1.0f };
        glm::vec4 ItemColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 HighlightColor{ 0.75f, 0.85f, 1.0f, 1.0f };
        float ItemHeight = 20.0f;
        // Runtime.
        bool IsExpanded = false;
        bool WasChanged = false;
        int HoveredItem = -1;
    };

    struct UICanvasGroupComponent {
        bool Enabled = true;
        float Alpha = 1.0f;
        bool Interactable = true;
        bool BlocksRaycasts = true;
        bool IgnoreParentGroups = false;
    };

    struct UIContentSizeFitterComponent {
        bool Enabled = true;
        ContentSizeFitMode HorizontalFit = ContentSizeFitMode::Unconstrained;
        ContentSizeFitMode VerticalFit = ContentSizeFitMode::Unconstrained;
    };

    struct UIAspectRatioFitterComponent {
        bool Enabled = true;
        AspectRatioMode AspectMode = AspectRatioMode::None;
        float AspectRatio = 1.0f; // width / height
    };

    struct UIGridLayoutGroupComponent {
        bool Enabled = true;
        glm::vec4 Padding{ 0.0f, 0.0f, 0.0f, 0.0f }; // left, right, top, bottom
        glm::vec2 CellSize{ 100.0f, 100.0f };
        glm::vec2 Spacing{ 0.0f, 0.0f };
        GridLayoutCorner StartCorner = GridLayoutCorner::UpperLeft;
        GridLayoutAxis StartAxis = GridLayoutAxis::Horizontal;
        GridLayoutConstraint Constraint = GridLayoutConstraint::Flexible;
        int ConstraintCount = 2;
    };

    // Text / image effects (Unity UI Shadow / Outline): extra copies of the graphic drawn under
    // it in Effect Color, offset by Effect Distance (Y down, pixels).
    struct UIShadowComponent {
        bool Enabled = true;
        glm::vec4 EffectColor{ 0.0f, 0.0f, 0.0f, 0.5f };
        glm::vec2 EffectDistance{ 1.0f, 1.0f };
        bool UseGraphicAlpha = true;
    };

    struct UIOutlineComponent {
        bool Enabled = true;
        glm::vec4 EffectColor{ 0.0f, 0.0f, 0.0f, 0.5f };
        glm::vec2 EffectDistance{ 1.0f, 1.0f };
        bool UseGraphicAlpha = true;
    };

}
