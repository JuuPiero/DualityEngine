#pragma once

namespace Duality {

    // Minimal dispatch-by-type event system, mirroring MyGameEngine's
    // Engine/Include/Core/Event.h -- deliberately scoped down to the two
    // events actually driven by a GLFW callback (window close/resize).
    // Panel-level mouse input (Scene view pan/zoom/gizmo, etc.) stays on
    // direct ImGui::GetIO() polling each frame, matching how MyGameEngine's
    // own viewport panels do it too -- the event system there is likewise
    // only ever used for window-level notifications, not per-widget input.
    enum class EventType {
        None = 0,
        WindowClose,
        WindowResize
    };

    class Event {
    public:
        virtual ~Event() = default;
        virtual EventType GetEventType() const = 0;
        bool Handled = false;
    };

    class WindowCloseEvent : public Event {
    public:
        static EventType StaticType() { return EventType::WindowClose; }
        EventType GetEventType() const override { return StaticType(); }
    };

    class WindowResizeEvent : public Event {
    public:
        WindowResizeEvent(int width, int height) : m_Width(width), m_Height(height) {}
        static EventType StaticType() { return EventType::WindowResize; }
        EventType GetEventType() const override { return StaticType(); }
        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }

    private:
        int m_Width, m_Height;
    };

    class EventDispatcher {
    public:
        explicit EventDispatcher(Event& event) : m_Event(event) {}

        template<typename T, typename F>
        bool Dispatch(const F& func) {
            if (m_Event.GetEventType() == T::StaticType()) {
                m_Event.Handled |= func(static_cast<T&>(m_Event));
                return true;
            }
            return false;
        }

    private:
        Event& m_Event;
    };

}
