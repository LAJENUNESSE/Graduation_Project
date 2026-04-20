#pragma once

// Stub — Vulkan framebuffer implementation will be added in Phase 5.

#include "Renderer/Framebuffer.h"

namespace Engine
{

    class VulkanFramebuffer : public Framebuffer
    {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec) : m_Spec(spec) {}
        ~VulkanFramebuffer() override = default;

        void Bind() override {}
        void Unbind() override {}
        void Resize(uint32_t width, uint32_t height) override { m_Spec.Width = width; m_Spec.Height = height; }

        int                            ReadPixel(uint32_t attachmentIndex, int x, int y) override { (void)attachmentIndex; (void)x; (void)y; return -1; }
        void                           ClearAttachment(uint32_t index, int value) override { (void)index; (void)value; }
        uint32_t                       GetColorAttachmentRendererID(uint32_t index = 0) const override { (void)index; return 0; }
        uint32_t                       GetDepthAttachmentRendererID() const override { return 0; }
        const FramebufferSpecification& GetSpecification() const override { return m_Spec; }

        void BindMSAA() override {}
        void BlitMSAA() override {}
        bool IsMSAAEnabled() const override { return false; }

    private:
        FramebufferSpecification m_Spec;
    };

} // namespace Engine
