#pragma once

#include <cstdint>

namespace Duality {

    // Minimal fixed-size color-only framebuffer object, used to render one
    // physical screen's worth of scene content off-screen so it can be shown
    // inside an ImGui panel via ImGui::Image(). Sized once at construction --
    // the 3DS's two screens are a fixed resolution forever, so there is no
    // resize/recreate path to support.
    class Framebuffer {
    public:
        Framebuffer(uint32_t width, uint32_t height);
        ~Framebuffer();

        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        void Bind() const;
        void Unbind() const;

        uint32_t GetColorAttachment() const { return m_ColorAttachment; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

    private:
        uint32_t m_RendererId = 0;
        uint32_t m_ColorAttachment = 0;
        uint32_t m_Width, m_Height;
    };

}
