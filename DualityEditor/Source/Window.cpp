#include "DualityEditor/Window.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <utility>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <vector>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "DualityEditor/EditorSettings.h"
#include "DualityEditor/EditorTheme.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Input/InputManager.h"

namespace Duality {

    namespace {
        // (KeyCode, GLFW key) -- only the desktop-meaningful half of
        // KeyCode; Gamepad*/D-Pad codes are left permanently false here,
        // same as this table just not mentioning them.
        constexpr std::pair<KeyCode, int> kDesktopKeyMap[] = {
            { KeyCode::W, GLFW_KEY_W }, { KeyCode::A, GLFW_KEY_A },
            { KeyCode::S, GLFW_KEY_S }, { KeyCode::D, GLFW_KEY_D },
            { KeyCode::Up, GLFW_KEY_UP }, { KeyCode::Down, GLFW_KEY_DOWN },
            { KeyCode::Left, GLFW_KEY_LEFT }, { KeyCode::Right, GLFW_KEY_RIGHT },
            { KeyCode::Space, GLFW_KEY_SPACE }, { KeyCode::Enter, GLFW_KEY_ENTER },
            { KeyCode::Escape, GLFW_KEY_ESCAPE },
        };
    }

    Window::Window(int width, int height, const std::string& title, bool background) : m_Width(width), m_Height(height) {
        if (!glfwInit()) {
            Log::Error("Window: glfwInit failed");
            std::exit(1);
        }

        if (background) {
            glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
            glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
        } else {
            glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
        }
        m_Handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if (!m_Handle) {
            Log::Error("Window: glfwCreateWindow failed");
            glfwTerminate();
            std::exit(1);
        }

        glfwMakeContextCurrent(m_Handle);
        glfwSwapInterval(1);

        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) {
            Log::Error("Window: glewInit failed");
            glfwTerminate();
            std::exit(1);
        }

        glfwSetWindowUserPointer(m_Handle, this);
        glfwSetWindowCloseCallback(m_Handle, [](GLFWwindow* handle) {
            auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
            if (self && self->m_EventCallback) {
                WindowCloseEvent e;
                self->m_EventCallback(e);
            }
        });
        glfwSetFramebufferSizeCallback(m_Handle, [](GLFWwindow* handle, int width, int height) {
            auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
            if (self && self->m_EventCallback) {
                WindowResizeEvent e(width, height);
                self->m_EventCallback(e);
            }
        });
        glfwSetDropCallback(m_Handle, [](GLFWwindow* handle, int count, const char** paths) {
            auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
            if (!self || !self->m_DropCallback)
                return;
            std::vector<std::string> files;
            files.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; i++)
                files.emplace_back(paths[i]);
            self->m_DropCallback(files);
        });

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        // Unity-like typography: the UI font is the OS's own Segoe UI (loaded from the Windows
        // font folder at runtime -- never copied into this repository, its license does not
        // allow redistribution), with a bold face for prefab overrides / section headers. Any
        // machine without it falls back to ImGui's built-in bitmap font scaled up, exactly the
        // editor's previous look. style.ScaleAllSizes() grows every widget dimension (padding,
        // spacing, scrollbars...) by the same factor so the UI stays proportionate.
        constexpr float UiScale = 1.3f;
        constexpr float FontPixels = 18.0f;
        ImGuiIO& io = ImGui::GetIO();

        std::filesystem::path fontsDirectory = "C:/Windows/Fonts";
        if (const char* windir = std::getenv("WINDIR"))
            fontsDirectory = std::filesystem::path(windir) / "Fonts";
        const std::filesystem::path regularPath = fontsDirectory / "segoeui.ttf";
        const std::filesystem::path boldPath = fontsDirectory / "segoeuib.ttf";

        // Editor-only icon font (Font Awesome Free, SIL OFL -- vendored in Assets/Fonts). It is
        // merged into each UI font so text and icons share one font stack; a missing file only
        // loses the glyphs and never prevents the editor from starting.
        static const ImWchar iconRanges[] = { 0xf000, 0xf8ff, 0 };
        std::filesystem::path iconFontPath;
        std::vector<std::filesystem::path> iconCandidates = {
            "Assets/Fonts/fa-solid-900.ttf", "DualityEditor/Assets/Fonts/fa-solid-900.ttf"
        };
        {
            // Next to the executable too (POST_BUILD copies Assets there), so the editor finds
            // it whatever its working directory is.
            wchar_t exePath[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
                iconCandidates.push_back(std::filesystem::path(exePath).parent_path() / "Assets/Fonts/fa-solid-900.ttf");
        }
        for (const std::filesystem::path& candidate : iconCandidates) {
            if (std::filesystem::exists(candidate)) {
                iconFontPath = candidate;
                break;
            }
        }

        auto mergeIcons = [&](float size) {
            if (iconFontPath.empty())
                return;
            ImFontConfig iconConfig;
            iconConfig.MergeMode = true;
            iconConfig.PixelSnapH = true;
            iconConfig.GlyphMinAdvanceX = size;
            iconConfig.GlyphOffset.y = 1.0f;
            io.Fonts->AddFontFromFileTTF(iconFontPath.string().c_str(), size * 0.85f, &iconConfig, iconRanges);
        };
        // Latin + Vietnamese (Latin Extended Additional) so localized names render.
        static const ImWchar textRanges[] = { 0x0020, 0x024F, 0x0300, 0x036F, 0x1E00, 0x1EFF, 0x2000, 0x206F, 0x2190, 0x21FF, 0 };

        ImFont* regular = nullptr;
        ImFont* bold = nullptr;
        if (std::filesystem::exists(regularPath)) {
            ImFontConfig config;
            config.OversampleH = 2;
            regular = io.Fonts->AddFontFromFileTTF(regularPath.string().c_str(), FontPixels, &config, textRanges);
            mergeIcons(FontPixels);
            if (regular && std::filesystem::exists(boldPath)) {
                bold = io.Fonts->AddFontFromFileTTF(boldPath.string().c_str(), FontPixels, &config, textRanges);
                mergeIcons(FontPixels);
            }
        }
        if (!regular) {
            ImFontConfig fontConfig;
            fontConfig.SizePixels = 13.0f * 1.5f;
            regular = io.Fonts->AddFontDefault(&fontConfig);
            mergeIcons(13.0f * 1.5f);
        }
        SetEditorFonts(regular, bold ? bold : regular);
        ImGui::GetStyle().ScaleAllSizes(UiScale);

        // The active theme is applied after scaling: switching themes later changes only
        // color/shape values and can never reset the user's UI scale or font atlas.
        ApplyEditorTheme(EditorThemeFromString(EditorSettings::Get().ColorTheme));

        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Multi-viewport (panels / popups dragged outside the main window become OS windows). Off
        // for automation runs so every popup is part of the main window's screenshot.
        if (!background)
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        ImGui_ImplGlfw_InitForOpenGL(m_Handle, true);
        ImGui_ImplOpenGL3_Init("#version 130");
    }

    Window::~Window() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(m_Handle);
        glfwTerminate();
    }

    void Window::BeginFrame() {
        glfwPollEvents();

        // ImGui's own NewFrame() (below) is what populates
        // io.WantCaptureKeyboard/Mouse for THIS frame, so gameplay input
        // polling reads last frame's capture flags -- one frame stale,
        // which in practice means input starts/stops one frame later than
        // the exact moment focus enters/leaves an ImGui widget. Not worth
        // reordering NewFrame() earlier just to close that gap.
        ImGuiIO& io = ImGui::GetIO();
        InputManager::BeginFrame();
        for (auto& [keyCode, glfwKey] : kDesktopKeyMap) {
            bool pressed = !io.WantCaptureKeyboard && glfwGetKey(m_Handle, glfwKey) == GLFW_PRESS;
            InputManager::SetKeyState(keyCode, pressed);
        }
        float horizontal = 0.0f, vertical = 0.0f;
        if (!io.WantCaptureKeyboard) {
            if (glfwGetKey(m_Handle, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_RIGHT) == GLFW_PRESS) horizontal += 1.0f;
            if (glfwGetKey(m_Handle, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_LEFT) == GLFW_PRESS) horizontal -= 1.0f;
            if (glfwGetKey(m_Handle, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_DOWN) == GLFW_PRESS) vertical += 1.0f;
            if (glfwGetKey(m_Handle, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_UP) == GLFW_PRESS) vertical -= 1.0f;
        }
        InputManager::SetAxis("Horizontal", horizontal);
        InputManager::SetAxis("Vertical", vertical);

        // Pointer state is NOT set here -- unlike keys/axes, resolving "which screen" (Top or
        // Bottom) a raw window-space mouse position falls over needs GamePanel's own knowledge
        // of where each screen's image is actually drawn this frame, which this class doesn't
        // have. GamePanel::OnImGuiRender calls InputManager::SetPointer itself once it draws the two
        // screen images, later in this same frame (Application::Run() renders it after
        // Window::BeginFrame) -- see its own comment.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void Window::EndFrame() {
        int displayW, displayH;
        glfwGetFramebufferSize(m_Handle, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.15f, 0.15f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::Render();
        if (!m_ScreenshotPath.empty())
            CaptureScreenshot();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            // Rendering secondary viewports makes their GL context current, so restore ours afterward.
            GLFWwindow* backupContext = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backupContext);
        }

        glfwSwapBuffers(m_Handle);
    }

    void Window::SetTitle(const std::string& title) {
        glfwSetWindowTitle(m_Handle, title.c_str());
    }

    void Window::SetShouldClose(bool shouldClose) {
        glfwSetWindowShouldClose(m_Handle, shouldClose ? GLFW_TRUE : GLFW_FALSE);
    }

    // Renders this frame's main-viewport draw data into an offscreen framebuffer and writes it
    // as a bottom-up 24-bit BMP. Offscreen so the capture never depends on the window being
    // visible/uncovered on the desktop (pixel ownership of the default framebuffer).
    void Window::CaptureScreenshot() {
        const std::string path = m_ScreenshotPath;
        m_ScreenshotPath.clear();
        ImDrawData* drawData = ImGui::GetDrawData();
        const int width = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
        const int height = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
        if (width <= 0 || height <= 0)
            return;

        GLuint fbo = 0, color = 0;
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.15f, 0.15f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(drawData);

        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);

        const int rowSize = (width * 3 + 3) & ~3;
        const uint32_t imageSize = static_cast<uint32_t>(rowSize * height);
        unsigned char header[54] = { 'B', 'M' };
        auto put32 = [&](int offset, uint32_t value) {
            for (int i = 0; i < 4; ++i)
                header[offset + i] = static_cast<unsigned char>(value >> (8 * i));
        };
        put32(2, 54 + imageSize);
        put32(10, 54);
        put32(14, 40);
        put32(18, static_cast<uint32_t>(width));
        put32(22, static_cast<uint32_t>(height));
        header[26] = 1;
        header[28] = 24;
        put32(34, imageSize);
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) {
            Log::Error("Window: could not write screenshot '" + path + "'");
            return;
        }
        std::fwrite(header, 1, sizeof(header), file);
        const unsigned char padding[3] = { 0, 0, 0 };
        for (int y = 0; y < height; ++y) {
            std::fwrite(pixels.data() + static_cast<size_t>(y) * width * 3, 1, static_cast<size_t>(width) * 3, file);
            std::fwrite(padding, 1, static_cast<size_t>(rowSize - width * 3), file);
        }
        std::fclose(file);
        Log::Info("Screenshot saved to '" + path + "'");
    }

    bool Window::ShouldClose() const {
        return glfwWindowShouldClose(m_Handle);
    }

    double Window::GetTime() const {
        return glfwGetTime();
    }

}
