// Registration of Scene/Components/UI.h. Display and field names are serialized: treat them as a
// data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterUIComponents() {
        Component<UIRectComponent>("UI Rect", ComponentCategory::UI, {
            MakeField("Enabled", &UIRectComponent::Enabled),
            MakeField("Anchor Min", &UIRectComponent::AnchorMin),
            MakeField("Anchor Max", &UIRectComponent::AnchorMax),
            MakeField("Pivot", &UIRectComponent::Pivot),
            MakeField("Anchored Position", &UIRectComponent::AnchoredPosition),
            MakeField("Size Delta", &UIRectComponent::SizeDelta),
            MakeField("Sort Order", &UIRectComponent::SortOrder),
        });

        Component<CanvasComponent>("Canvas", ComponentCategory::UI, {
            MakeField("Enabled", &CanvasComponent::Enabled),
            MakeField("Screen", &CanvasComponent::Screen),
            MakeField("Render Mode", &CanvasComponent::RenderMode),
            MakeField("Sort Order", &CanvasComponent::SortOrder),
            MakeField("Scale Factor", &CanvasComponent::ScaleFactor),
            MakeField("Target Camera", &CanvasComponent::TargetCamera),
        });

        Component<UILayoutGroupComponent>("UI Layout Group", ComponentCategory::UI, {
            MakeField("Enabled", &UILayoutGroupComponent::Enabled),
            MakeField("Layout", &UILayoutGroupComponent::Layout),
            MakeField("Spacing", &UILayoutGroupComponent::Spacing),
            MakeField("Padding", &UILayoutGroupComponent::Padding),
            MakeField("Control Width", &UILayoutGroupComponent::ChildControlWidth),
            MakeField("Control Height", &UILayoutGroupComponent::ChildControlHeight),
        });

        Component<UIImageComponent>("UI Image", ComponentCategory::UI, {
            MakeField("Enabled", &UIImageComponent::Enabled),
            MakeColorField("Color", &UIImageComponent::Color),
            MakeField("Texture", &UIImageComponent::Texture),
            MakeField("Slice Border", &UIImageComponent::SliceBorder),
        });

        // IsHovered/IsPressed/WasClicked are runtime state, deliberately not registered.
        Component<UIButtonComponent>("UI Button", ComponentCategory::UI, {
            MakeField("Enabled", &UIButtonComponent::Enabled),
            MakeColorField("Normal Color", &UIButtonComponent::NormalColor),
            MakeColorField("Hover Color", &UIButtonComponent::HoverColor),
            MakeColorField("Pressed Color", &UIButtonComponent::PressedColor),
        });

        Component<UITextComponent>("UI Text", ComponentCategory::UI, {
            MakeField("Enabled", &UITextComponent::Enabled),
            MakeField("Text", &UITextComponent::Text),
            MakeField("Font", &UITextComponent::Font),
            MakeField("Font Size", &UITextComponent::FontSize),
            MakeField("Color", &UITextComponent::Color),
            MakeField("Alignment", &UITextComponent::Alignment),
        });

        Component<UISliderComponent>("UI Slider", ComponentCategory::UI, {
            MakeField("Enabled", &UISliderComponent::Enabled),
            MakeField("Value", &UISliderComponent::Value),
            MakeField("Min Value", &UISliderComponent::MinValue),
            MakeField("Max Value", &UISliderComponent::MaxValue),
        });

        Component<UIToggleComponent>("UI Toggle", ComponentCategory::UI, {
            MakeField("Enabled", &UIToggleComponent::Enabled),
            MakeField("Is On", &UIToggleComponent::IsOn),
        });

        Component<UIInputFieldComponent>("UI Input Field", ComponentCategory::UI, {
            MakeField("Enabled", &UIInputFieldComponent::Enabled),
            MakeField("Text", &UIInputFieldComponent::Text),
            MakeField("Placeholder", &UIInputFieldComponent::Placeholder),
        });

        Component<UIScrollRectComponent>("UI Scroll Rect", ComponentCategory::UI, {
            MakeField("Enabled", &UIScrollRectComponent::Enabled),
            Tip(MakeField("Content", &UIScrollRectComponent::Content), "Child UI Rect that is dragged."),
            MakeField("Horizontal", &UIScrollRectComponent::Horizontal),
            MakeField("Vertical", &UIScrollRectComponent::Vertical),
            MakeEnumField("Movement Type", &UIScrollRectComponent::MovementType, { "Unrestricted", "Elastic", "Clamped" }),
            Ranged(MakeField("Elasticity", &UIScrollRectComponent::Elasticity), 0.0f, 1.0f),
            MakeField("Inertia", &UIScrollRectComponent::Inertia),
            Ranged(MakeField("Deceleration Rate", &UIScrollRectComponent::DecelerationRate), 0.0f, 1.0f),
            MakeField("Scroll Sensitivity", &UIScrollRectComponent::ScrollSensitivity),
            Tip(MakeField("Viewport", &UIScrollRectComponent::Viewport), "Empty = this entity's rect."),
            MakeField("Horizontal Scrollbar", &UIScrollRectComponent::HorizontalScrollbar),
            MakeField("Vertical Scrollbar", &UIScrollRectComponent::VerticalScrollbar),
        });

        Component<UIScrollbarComponent>("UI Scrollbar", ComponentCategory::UI, {
            MakeField("Enabled", &UIScrollbarComponent::Enabled),
            MakeEnumField("Direction", &UIScrollbarComponent::Direction, { "Left To Right", "Right To Left", "Bottom To Top", "Top To Bottom" }),
            Ranged(MakeField("Value", &UIScrollbarComponent::Value), 0.0f, 1.0f),
            Ranged(MakeField("Size", &UIScrollbarComponent::Size), 0.0f, 1.0f),
            Ranged(MakeField("Number Of Steps", &UIScrollbarComponent::NumberOfSteps), 0.0f, 11.0f, "0 = continuous."),
            MakeColorField("Handle Color", &UIScrollbarComponent::HandleColor),
        });

        Component<UIMaskComponent>("UI Mask", ComponentCategory::UI, {
            Tip(MakeField("Enabled", &UIMaskComponent::Enabled), "Clips children to this rect (GPU scissor, also on 3DS)."),
            MakeField("Show Mask Graphic", &UIMaskComponent::ShowMaskGraphic),
        });

        Component<UIRectMask2DComponent>("UI Rect Mask 2D", ComponentCategory::UI, {
            Tip(MakeField("Enabled", &UIRectMask2DComponent::Enabled), "Clips children to this rect (GPU scissor, also on 3DS)."),
            Tip(MakeField("Padding", &UIRectMask2DComponent::Padding), "Left, top, right, bottom (pixels, inwards)."),
        });

        Component<UIDropdownComponent>("UI Dropdown", ComponentCategory::UI, {
            MakeField("Enabled", &UIDropdownComponent::Enabled),
            Tip(MakeField("Options", &UIDropdownComponent::Options), "Separated by '|'."),
            MakeField("Value", &UIDropdownComponent::Value),
            MakeField("Font", &UIDropdownComponent::Font),
            MakeField("Font Size", &UIDropdownComponent::FontSize),
            MakeColorField("Text Color", &UIDropdownComponent::TextColor),
            MakeColorField("Item Color", &UIDropdownComponent::ItemColor),
            MakeColorField("Highlight Color", &UIDropdownComponent::HighlightColor),
            MakeField("Item Height", &UIDropdownComponent::ItemHeight),
        });

        Component<UICanvasGroupComponent>("Canvas Group", ComponentCategory::UI, {
            MakeField("Enabled", &UICanvasGroupComponent::Enabled),
            Ranged(MakeField("Alpha", &UICanvasGroupComponent::Alpha), 0.0f, 1.0f),
            MakeField("Interactable", &UICanvasGroupComponent::Interactable),
            MakeField("Blocks Raycasts", &UICanvasGroupComponent::BlocksRaycasts),
            MakeField("Ignore Parent Groups", &UICanvasGroupComponent::IgnoreParentGroups),
        });

        const std::vector<std::string> fitModes{ "Unconstrained", "Min Size", "Preferred Size" };
        Component<UIContentSizeFitterComponent>("UI Content Size Fitter", ComponentCategory::UI, {
            MakeField("Enabled", &UIContentSizeFitterComponent::Enabled),
            MakeEnumField("Horizontal Fit", &UIContentSizeFitterComponent::HorizontalFit, fitModes),
            MakeEnumField("Vertical Fit", &UIContentSizeFitterComponent::VerticalFit, fitModes),
        });

        Component<UIAspectRatioFitterComponent>("UI Aspect Ratio Fitter", ComponentCategory::UI, {
            MakeField("Enabled", &UIAspectRatioFitterComponent::Enabled),
            MakeEnumField("Aspect Mode", &UIAspectRatioFitterComponent::AspectMode,
                { "None", "Width Controls Height", "Height Controls Width", "Fit In Parent", "Envelope Parent" }),
            Tip(MakeField("Aspect Ratio", &UIAspectRatioFitterComponent::AspectRatio), "Width / height."),
        });

        Component<UIGridLayoutGroupComponent>("UI Grid Layout Group", ComponentCategory::UI, {
            MakeField("Enabled", &UIGridLayoutGroupComponent::Enabled),
            Tip(MakeField("Padding", &UIGridLayoutGroupComponent::Padding), "Left, right, top, bottom."),
            MakeField("Cell Size", &UIGridLayoutGroupComponent::CellSize),
            MakeField("Spacing", &UIGridLayoutGroupComponent::Spacing),
            MakeEnumField("Start Corner", &UIGridLayoutGroupComponent::StartCorner, { "Upper Left", "Upper Right", "Lower Left", "Lower Right" }),
            MakeEnumField("Start Axis", &UIGridLayoutGroupComponent::StartAxis, { "Horizontal", "Vertical" }),
            MakeEnumField("Constraint", &UIGridLayoutGroupComponent::Constraint, { "Flexible", "Fixed Column Count", "Fixed Row Count" }),
            MakeField("Constraint Count", &UIGridLayoutGroupComponent::ConstraintCount),
        });

        Component<UIShadowComponent>("UI Shadow", ComponentCategory::UI, {
            MakeField("Enabled", &UIShadowComponent::Enabled),
            MakeColorField("Effect Color", &UIShadowComponent::EffectColor),
            Tip(MakeField("Effect Distance", &UIShadowComponent::EffectDistance), "Pixels, +Y down."),
            MakeField("Use Graphic Alpha", &UIShadowComponent::UseGraphicAlpha),
        });

        Component<UIOutlineComponent>("UI Outline", ComponentCategory::UI, {
            MakeField("Enabled", &UIOutlineComponent::Enabled),
            MakeColorField("Effect Color", &UIOutlineComponent::EffectColor),
            MakeField("Effect Distance", &UIOutlineComponent::EffectDistance),
            MakeField("Use Graphic Alpha", &UIOutlineComponent::UseGraphicAlpha),
        });
    }

}
