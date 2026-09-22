#include "DualityEditor/EditorTheme.h"

#include <imgui.h>

namespace Duality {

    EditorTheme EditorThemeFromString(const std::string& name) {
        if (name == "Pink") return EditorTheme::Pink;
        if (name == "Dark") return EditorTheme::Dark;
        return EditorTheme::Blue;
    }

    const char* ToString(EditorTheme theme) {
        switch (theme) {
            case EditorTheme::Pink: return "Pink";
            case EditorTheme::Dark: return "Dark";
            case EditorTheme::Blue:
            default: return "Blue";
        }
    }

    namespace {
        void ApplyCommonShape(ImGuiStyle& style) {
            style.WindowRounding = 5.0f;
            style.ChildRounding = 4.0f;
            style.FrameRounding = 3.0f;
            style.GrabRounding = 3.0f;
            style.TabRounding = 4.0f;
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

    void ApplyEditorTheme(EditorTheme theme) {
        ImGuiStyle& style = ImGui::GetStyle();
        // Start from ImGui's complete readable dark palette. Each preset then changes its
        // identity colors, avoiding a partial custom palette with unreadable widgets.
        ImGui::StyleColorsDark(&style);
        ApplyCommonShape(style);
        switch (theme) {
            case EditorTheme::Pink: ApplyPink(style); break;
            case EditorTheme::Dark: ApplyDark(style); break;
            case EditorTheme::Blue:
            default: ApplyBlue(style); break;
        }
    }

}
