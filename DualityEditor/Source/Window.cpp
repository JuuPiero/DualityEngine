#include "DualityEditor/Window.h"

#include <cstdlib>
#include <utility>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Input/Input.h"

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

    Window::Window(int width, int height, const std::string& title) : m_Width(width), m_Height(height) {
        if (!glfwInit()) {
            Log::Error("Window: glfwInit failed");
            std::exit(1);
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

        // Bigger UI across the board (text, row height, tabs, menu bar, padding, scrollbars,
        // ...) -- no TTF is vendored, so this bumps ImGui's own built-in bitmap font's pixel
        // size (13px default -> UiScale) rather than adding a font dependency; the bitmap
        // reads a bit blockier at this size than a real TTF would, but stays sharp enough to
        // be clearly worth it for readability. style.ScaleAllSizes() grows every other widget
        // dimension (padding/spacing/rounding/scrollbar width/...) by the same factor so the
        // whole UI feels proportionate, not just the text.
        constexpr float UiScale = 1.5f;
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 13.0f * UiScale;
        io.Fonts->AddFontDefault(&fontConfig);
        ImGui::GetStyle().ScaleAllSizes(UiScale);

        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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
        Input::BeginFrame();
        for (auto& [keyCode, glfwKey] : kDesktopKeyMap) {
            bool pressed = !io.WantCaptureKeyboard && glfwGetKey(m_Handle, glfwKey) == GLFW_PRESS;
            Input::SetKeyState(keyCode, pressed);
        }
        float horizontal = 0.0f, vertical = 0.0f;
        if (!io.WantCaptureKeyboard) {
            if (glfwGetKey(m_Handle, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_RIGHT) == GLFW_PRESS) horizontal += 1.0f;
            if (glfwGetKey(m_Handle, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_LEFT) == GLFW_PRESS) horizontal -= 1.0f;
            if (glfwGetKey(m_Handle, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_DOWN) == GLFW_PRESS) vertical += 1.0f;
            if (glfwGetKey(m_Handle, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(m_Handle, GLFW_KEY_UP) == GLFW_PRESS) vertical -= 1.0f;
        }
        Input::SetAxis("Horizontal", horizontal);
        Input::SetAxis("Vertical", vertical);

        if (io.WantCaptureMouse) {
            Input::SetPointer(false, Input::GetPointerPosition());
        } else {
            double mouseX, mouseY;
            glfwGetCursorPos(m_Handle, &mouseX, &mouseY);
            bool down = glfwGetMouseButton(m_Handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            Input::SetPointer(down, { static_cast<float>(mouseX), static_cast<float>(mouseY) });
        }

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

    bool Window::ShouldClose() const {
        return glfwWindowShouldClose(m_Handle);
    }

    double Window::GetTime() const {
        return glfwGetTime();
    }

}
