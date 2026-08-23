#pragma once

#include <functional>
#include <string>
#include <vector>

#include "DualityEditor/Event.h"

struct GLFWwindow;

namespace Duality {

    // Owns the GLFW window + GL context and the ImGui context/GLFW+OpenGL3
    // backend bound to it -- everything Main.cpp used to set up by hand
    // before the per-frame loop. BeginFrame/EndFrame bracket one ImGui
    // frame (poll events -> new frame ... panels ... -> render -> swap).
    // GLFW's close/resize callbacks are forwarded as Event objects through
    // SetEventCallback rather than polled directly, so Application doesn't
    // need to know anything about GLFW itself.
    class Window {
    public:
        Window(int width, int height, const std::string& title);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        void BeginFrame();
        void EndFrame();

        bool ShouldClose() const;
        double GetTime() const;
        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }
        GLFWwindow* GetNativeWindow() const { return m_Handle; }

        void SetEventCallback(const std::function<void(Event&)>& callback) { m_EventCallback = callback; }

        // OS file drop (e.g. dragging files in from Windows Explorer).
        void SetDropCallback(const std::function<void(const std::vector<std::string>&)>& callback) { m_DropCallback = callback; }

    private:
        GLFWwindow* m_Handle = nullptr;
        int m_Width, m_Height;
        std::function<void(Event&)> m_EventCallback;
        std::function<void(const std::vector<std::string>&)> m_DropCallback;
    };

}
