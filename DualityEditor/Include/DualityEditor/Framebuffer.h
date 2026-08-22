#pragma once

#include <cstdint>

namespace Duality {

    // Minimal color-only framebuffer object, used to render scene content
    // off-screen so it can be shown inside an ImGui panel via
    // ImGui::Image(). The Top/Bottom screen framebuffers never call
    // Resize() -- those are a fixed resolution forever, matching real 3DS
    // hardware -- but the Editor's Scene view is desktop-only tooling with
    // no hardware size constraint, and needs to track its ImGui panel's
    // size like a real editor viewport.
    class Framebuffer {
    public:
        Framebuffer(uint32_t width, uint32_t height);
        ~Framebuffer();

        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        void Bind() const;
        void Unbind() const;

        // Recreates the color attachment at a new size; a no-op if the size
        // is unchanged (or degenerate) so it's cheap to call every frame
        // with the current panel size.
        void Resize(uint32_t width, uint32_t height);

        uint32_t GetColorAttachment() const { return m_ColorAttachment; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

    private:
        void CreateAttachments();

        uint32_t m_RendererId = 0;
        uint32_t m_ColorAttachment = 0;
        uint32_t m_Width, m_Height;
    };

}
