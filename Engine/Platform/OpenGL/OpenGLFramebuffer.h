#pragma once

#include "Renderer/Framebuffer.h"

namespace Engine
{

    class OpenGLFramebuffer : public Framebuffer
    {
    public:
        OpenGLFramebuffer(const FramebufferSpecification& spec);
        ~OpenGLFramebuffer() override;

        void Bind() override;
        void Unbind() override;

        void Resize(uint32_t width, uint32_t height) override;

        uint32_t GetColorAttachmentRendererID() const override
        {
            return m_ColorAttachment;
        }

        const FramebufferSpecification& GetSpecification() const override
        {
            return m_Specification;
        }

    private:
        void Invalidate();

        uint32_t m_RendererID = 0;
        uint32_t m_ColorAttachment = 0;
        uint32_t m_DepthAttachment = 0;
        FramebufferSpecification m_Specification;
    };

} // namespace Engine
