#include "DualityEditor/EditorTheme.h"

#include <imgui.h>

namespace Duality {

    EditorTheme EditorThemeFromString(const std::string& name) {
        if (name == "Pink") return EditorTheme::Pink;
        if (name == "Dark") return EditorTheme::Dark;
        if (name == "Blue") return EditorTheme::Blue;
        return EditorTheme::Unity;
    }

    const char* ToString(EditorTheme theme) {
        switch (theme) {
            case EditorTheme::Pink: return "Pink";
            case EditorTheme::Dark: return "Dark";
            case EditorTheme::Blue: return "Blue";
            case EditorTheme::Unity:
            default: return "Unity";
        }
    }

    namespace {
        void ApplyCommonShape(ImGuiStyle& style) {
            style.WindowRounding = 5.0f;
            style.ChildRounding = 4.0f;
            style.FrameRounding = 3.0f;
            style.GrabRounding = 3.0f;
            style.TabRounding = 4.0f;
            style.WindowBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.TabBorderSize = 0.0f;
            style.WindowMenuButtonPosition = ImGuiDir_Left;
        }

        // ImGui 1.90 split the old tab colors into selected/dimmed/overline variants and
        // introduced separate docking colors. Leaving any of those on StyleColorsDark() makes
        // a supposedly neutral/pink theme retain conspicuous blue dock highlights.
        void ApplyAccentChrome(ImGuiStyle& style, const ImVec4& windowBg, const ImVec4& surface,
            const ImVec4& tab, const ImVec4& selected, const ImVec4& hover, const ImVec4& active,
            const ImVec4& border) {
            auto& c = style.Colors;
            c[ImGuiCol_TitleBg] = surface;
            c[ImGuiCol_TitleBgActive] = selected;
            c[ImGuiCol_TitleBgCollapsed] = tab;
            c[ImGuiCol_MenuBarBg] = surface;
            c[ImGuiCol_ScrollbarBg] = windowBg;
            c[ImGuiCol_ScrollbarGrab] = border;
            c[ImGuiCol_ScrollbarGrabHovered] = hover;
            c[ImGuiCol_ScrollbarGrabActive] = active;
            c[ImGuiCol_SliderGrabActive] = active;
            c[ImGuiCol_Separator] = border;
            c[ImGuiCol_SeparatorHovered] = hover;
            c[ImGuiCol_SeparatorActive] = active;
            c[ImGuiCol_ResizeGrip] = ImVec4(hover.x, hover.y, hover.z, 0.40f);
            c[ImGuiCol_ResizeGripHovered] = hover;
            c[ImGuiCol_ResizeGripActive] = active;
            c[ImGuiCol_Tab] = tab;
            c[ImGuiCol_TabSelected] = selected;
            c[ImGuiCol_TabSelectedOverline] = active;
            c[ImGuiCol_TabDimmed] = tab;
            c[ImGuiCol_TabDimmedSelected] = selected;
            c[ImGuiCol_TabDimmedSelectedOverline] = active;
            c[ImGuiCol_DockingPreview] = ImVec4(active.x, active.y, active.z, 0.55f);
            c[ImGuiCol_DockingEmptyBg] = windowBg;
            c[ImGuiCol_TableHeaderBg] = surface;
            c[ImGuiCol_TableBorderStrong] = border;
            c[ImGuiCol_TableBorderLight] = border;
            c[ImGuiCol_TextLink] = hover;
            c[ImGuiCol_TextSelectedBg] = ImVec4(active.x, active.y, active.z, 0.35f);
            c[ImGuiCol_DragDropTarget] = hover;
            c[ImGuiCol_NavHighlight] = hover;
        }

        void ApplyBlue(ImGuiStyle& style) {
            auto& c = style.Colors;
            c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.090f, 1.0f);
            c[ImGuiCol_ChildBg] = ImVec4(0.070f, 0.080f, 0.110f, 1.0f);
            c[ImGuiCol_FrameBg] = ImVec4(0.105f, 0.125f, 0.170f, 1.0f);
            c[ImGuiCol_FrameBgHovered] = ImVec4(0.145f, 0.185f, 0.250f, 1.0f);
            c[ImGuiCol_Button] = ImVec4(0.105f, 0.240f, 0.420f, 1.0f);
            c[ImGuiCol_ButtonHovered] = ImVec4(0.140f, 0.340f, 0.580f, 1.0f);
            c[ImGuiCol_ButtonActive] = ImVec4(0.090f, 0.200f, 0.360f, 1.0f);
            c[ImGuiCol_Header] = ImVec4(0.100f, 0.250f, 0.450f, 0.75f);
            c[ImGuiCol_HeaderHovered] = ImVec4(0.140f, 0.340f, 0.580f, 0.85f);
            c[ImGuiCol_HeaderActive] = ImVec4(0.120f, 0.300f, 0.530f, 1.0f);
            c[ImGuiCol_Tab] = ImVec4(0.080f, 0.110f, 0.160f, 1.0f);
            c[ImGuiCol_TabActive] = ImVec4(0.110f, 0.250f, 0.440f, 1.0f);
            c[ImGuiCol_TabHovered] = ImVec4(0.140f, 0.340f, 0.580f, 1.0f);
            ApplyAccentChrome(style,
                ImVec4(0.055f, 0.065f, 0.090f, 1.0f), ImVec4(0.070f, 0.080f, 0.110f, 1.0f),
                ImVec4(0.080f, 0.110f, 0.160f, 1.0f), ImVec4(0.110f, 0.250f, 0.440f, 1.0f),
                ImVec4(0.140f, 0.340f, 0.580f, 1.0f), ImVec4(0.090f, 0.200f, 0.360f, 1.0f),
                ImVec4(0.180f, 0.220f, 0.300f, 1.0f));
        }

        void ApplyPink(ImGuiStyle& style) {
            auto& c = style.Colors;
            c[ImGuiCol_WindowBg] = ImVec4(0.090f, 0.045f, 0.075f, 1.0f);
            c[ImGuiCol_ChildBg] = ImVec4(0.115f, 0.055f, 0.095f, 1.0f);
            c[ImGuiCol_FrameBg] = ImVec4(0.180f, 0.075f, 0.145f, 1.0f);
            c[ImGuiCol_FrameBgHovered] = ImVec4(0.285f, 0.105f, 0.220f, 1.0f);
            c[ImGuiCol_Button] = ImVec4(0.470f, 0.105f, 0.330f, 1.0f);
            c[ImGuiCol_ButtonHovered] = ImVec4(0.680f, 0.145f, 0.465f, 1.0f);
            c[ImGuiCol_ButtonActive] = ImVec4(0.365f, 0.070f, 0.255f, 1.0f);
            c[ImGuiCol_Header] = ImVec4(0.510f, 0.115f, 0.365f, 0.72f);
            c[ImGuiCol_HeaderHovered] = ImVec4(0.710f, 0.155f, 0.500f, 0.86f);
            c[ImGuiCol_HeaderActive] = ImVec4(0.620f, 0.125f, 0.435f, 1.0f);
            c[ImGuiCol_Tab] = ImVec4(0.145f, 0.060f, 0.115f, 1.0f);
            c[ImGuiCol_TabActive] = ImVec4(0.430f, 0.090f, 0.305f, 1.0f);
            c[ImGuiCol_TabHovered] = ImVec4(0.680f, 0.145f, 0.465f, 1.0f);
            c[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.48f, 0.78f, 1.0f);
            c[ImGuiCol_SliderGrab] = ImVec4(0.88f, 0.28f, 0.62f, 1.0f);
            ApplyAccentChrome(style,
                ImVec4(0.090f, 0.045f, 0.075f, 1.0f), ImVec4(0.115f, 0.055f, 0.095f, 1.0f),
                ImVec4(0.145f, 0.060f, 0.115f, 1.0f), ImVec4(0.430f, 0.090f, 0.305f, 1.0f),
                ImVec4(0.680f, 0.145f, 0.465f, 1.0f), ImVec4(0.365f, 0.070f, 0.255f, 1.0f),
                ImVec4(0.310f, 0.120f, 0.250f, 1.0f));
        }

        void ApplyDark(ImGuiStyle& style) {
            auto& c = style.Colors;
            c[ImGuiCol_WindowBg] = ImVec4(0.028f, 0.030f, 0.035f, 1.0f);
            c[ImGuiCol_ChildBg] = ImVec4(0.042f, 0.045f, 0.052f, 1.0f);
            c[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.058f, 0.066f, 0.98f);
            c[ImGuiCol_Border] = ImVec4(0.190f, 0.195f, 0.215f, 0.55f);
            c[ImGuiCol_FrameBg] = ImVec4(0.105f, 0.110f, 0.125f, 1.0f);
            c[ImGuiCol_FrameBgHovered] = ImVec4(0.165f, 0.170f, 0.190f, 1.0f);
            c[ImGuiCol_FrameBgActive] = ImVec4(0.205f, 0.210f, 0.235f, 1.0f);
            c[ImGuiCol_Button] = ImVec4(0.155f, 0.160f, 0.180f, 1.0f);
            c[ImGuiCol_ButtonHovered] = ImVec4(0.245f, 0.250f, 0.280f, 1.0f);
            c[ImGuiCol_ButtonActive] = ImVec4(0.110f, 0.115f, 0.130f, 1.0f);
            c[ImGuiCol_Header] = ImVec4(0.180f, 0.185f, 0.210f, 0.75f);
            c[ImGuiCol_HeaderHovered] = ImVec4(0.280f, 0.285f, 0.320f, 0.85f);
            c[ImGuiCol_HeaderActive] = ImVec4(0.225f, 0.230f, 0.260f, 1.0f);
            c[ImGuiCol_Tab] = ImVec4(0.075f, 0.078f, 0.090f, 1.0f);
            c[ImGuiCol_TabActive] = ImVec4(0.180f, 0.185f, 0.210f, 1.0f);
            c[ImGuiCol_TabHovered] = ImVec4(0.285f, 0.290f, 0.325f, 1.0f);
            c[ImGuiCol_CheckMark] = ImVec4(0.82f, 0.84f, 0.90f, 1.0f);
            c[ImGuiCol_SliderGrab] = ImVec4(0.62f, 0.65f, 0.72f, 1.0f);
            ApplyAccentChrome(style,
                ImVec4(0.028f, 0.030f, 0.035f, 1.0f), ImVec4(0.042f, 0.045f, 0.052f, 1.0f),
                ImVec4(0.075f, 0.078f, 0.090f, 1.0f), ImVec4(0.180f, 0.185f, 0.210f, 1.0f),
                ImVec4(0.285f, 0.290f, 0.325f, 1.0f), ImVec4(0.225f, 0.230f, 0.260f, 1.0f),
                ImVec4(0.190f, 0.195f, 0.215f, 1.0f));
        }
    }

    namespace {
        // Unity's dark editor skin: mid-grey panels (#383838), darker tab strip / toolbars
        // (#282828), recessed input fields (#2A2A2A), raised buttons (#585858), the blue
        // selection (#2C5D87), light grey text (#D2D2D2) and almost square corners.
        void ApplyUnity(ImGuiStyle& style) {
            auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
            auto& c = style.Colors;
            const ImVec4 panel = rgb(56, 56, 56);
            const ImVec4 dark = rgb(40, 40, 40);
            const ImVec4 darker = rgb(25, 25, 25);
            const ImVec4 field = rgb(42, 42, 42);
            const ImVec4 fieldHover = rgb(48, 48, 48);
            const ImVec4 button = rgb(88, 88, 88);
            const ImVec4 buttonHover = rgb(103, 103, 103);
            const ImVec4 buttonActive = rgb(70, 96, 124);
            const ImVec4 selection = rgb(44, 93, 135);
            const ImVec4 selectionHover = rgb(58, 114, 160);
            const ImVec4 border = rgb(26, 26, 26);
            const ImVec4 accent = rgb(58, 121, 187);

            c[ImGuiCol_Text] = rgb(210, 210, 210);
            c[ImGuiCol_TextDisabled] = rgb(128, 128, 128);
            c[ImGuiCol_WindowBg] = panel;
            c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
            c[ImGuiCol_PopupBg] = rgb(48, 48, 48, 0.99f);
            c[ImGuiCol_Border] = border;
            c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
            c[ImGuiCol_FrameBg] = field;
            c[ImGuiCol_FrameBgHovered] = fieldHover;
            c[ImGuiCol_FrameBgActive] = rgb(36, 36, 36);
            c[ImGuiCol_TitleBg] = dark;
            c[ImGuiCol_TitleBgActive] = dark;
            c[ImGuiCol_TitleBgCollapsed] = dark;
            c[ImGuiCol_MenuBarBg] = dark;
            c[ImGuiCol_ScrollbarBg] = rgb(48, 48, 48);
            c[ImGuiCol_ScrollbarGrab] = rgb(94, 94, 94);
            c[ImGuiCol_ScrollbarGrabHovered] = rgb(110, 110, 110);
            c[ImGuiCol_ScrollbarGrabActive] = rgb(128, 128, 128);
            c[ImGuiCol_CheckMark] = rgb(220, 220, 220);
            c[ImGuiCol_SliderGrab] = rgb(150, 150, 150);
            c[ImGuiCol_SliderGrabActive] = rgb(190, 190, 190);
            c[ImGuiCol_Button] = button;
            c[ImGuiCol_ButtonHovered] = buttonHover;
            c[ImGuiCol_ButtonActive] = buttonActive;
            c[ImGuiCol_Header] = rgb(62, 62, 62);
            c[ImGuiCol_HeaderHovered] = rgb(72, 72, 72);
            c[ImGuiCol_HeaderActive] = selection;
            c[ImGuiCol_Separator] = border;
            c[ImGuiCol_SeparatorHovered] = accent;
            c[ImGuiCol_SeparatorActive] = accent;
            c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
            c[ImGuiCol_ResizeGripHovered] = accent;
            c[ImGuiCol_ResizeGripActive] = accent;
            c[ImGuiCol_Tab] = dark;
            c[ImGuiCol_TabHovered] = rgb(64, 64, 64);
            c[ImGuiCol_TabSelected] = panel;
            c[ImGuiCol_TabSelectedOverline] = accent;
            c[ImGuiCol_TabDimmed] = dark;
            c[ImGuiCol_TabDimmedSelected] = panel;
            c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
            c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.6f);
            c[ImGuiCol_DockingEmptyBg] = darker;
            c[ImGuiCol_PlotLines] = rgb(160, 160, 160);
            c[ImGuiCol_PlotHistogram] = accent;
            c[ImGuiCol_TableHeaderBg] = dark;
            c[ImGuiCol_TableBorderStrong] = border;
            c[ImGuiCol_TableBorderLight] = rgb(48, 48, 48);
            c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
            c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
            c[ImGuiCol_TextLink] = rgb(110, 160, 230);
            c[ImGuiCol_TextSelectedBg] = ImVec4(selection.x, selection.y, selection.z, 0.8f);
            c[ImGuiCol_DragDropTarget] = rgb(80, 160, 255);
            c[ImGuiCol_NavHighlight] = selectionHover;
            c[ImGuiCol_NavWindowingHighlight] = rgb(255, 255, 255, 0.7f);
            c[ImGuiCol_NavWindowingDimBg] = rgb(0, 0, 0, 0.4f);
            c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.45f);

            style.WindowRounding = 0.0f;
            style.ChildRounding = 0.0f;
            style.FrameRounding = 2.0f;
            style.PopupRounding = 2.0f;
            style.GrabRounding = 2.0f;
            style.ScrollbarRounding = 4.0f;
            style.TabRounding = 2.0f;
            style.WindowBorderSize = 1.0f;
            style.FrameBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.TabBorderSize = 0.0f;
            style.WindowMenuButtonPosition = ImGuiDir_None;
        }
    }

    void ApplyEditorTheme(EditorTheme theme) {
        ImGuiStyle& style = ImGui::GetStyle();
        // Start from ImGui's complete readable dark palette. Each preset then changes its
        // identity colors, avoiding a partial custom palette with unreadable widgets.
        ImGui::StyleColorsDark(&style);
        ApplyCommonShape(style);
        switch (theme) {
            case EditorTheme::Pink: ApplyPink(style); break;
            case EditorTheme::Dark: ApplyDark(style); break;
            case EditorTheme::Blue: ApplyBlue(style); break;
            case EditorTheme::Unity:
            default: ApplyUnity(style); break;
        }
    }

    namespace {
        ImFont* s_RegularFont = nullptr;
        ImFont* s_BoldFont = nullptr;
    }

    void SetEditorFonts(ImFont* regular, ImFont* bold) {
        s_RegularFont = regular;
        s_BoldFont = bold ? bold : regular;
    }

    ImFont* EditorRegularFont() { return s_RegularFont ? s_RegularFont : ImGui::GetFont(); }
    ImFont* EditorBoldFont() { return s_BoldFont ? s_BoldFont : ImGui::GetFont(); }

    namespace EditorColors {
        unsigned int Selection() { return IM_COL32(44, 93, 135, 255); }
        unsigned int SelectionInactive() { return IM_COL32(77, 77, 77, 255); }
        unsigned int PrefabText() { return IM_COL32(124, 178, 238, 255); }
        unsigned int PrefabMissing() { return IM_COL32(235, 100, 90, 255); }
        unsigned int InactiveText() { return IM_COL32(128, 128, 128, 255); }
        unsigned int Ping() { return IM_COL32(250, 200, 60, 255); }
        unsigned int AxisX() { return IM_COL32(219, 62, 29, 255); }
        unsigned int AxisY() { return IM_COL32(139, 220, 0, 255); }
        unsigned int AxisZ() { return IM_COL32(58, 122, 248, 255); }
    }

}
