#pragma once

#include "Renderer/Framebuffer.h"

#include <vector>
#include <vulkan/vulkan.h>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace Engine
{

    class VulkanFramebuffer : public Framebuffer
    {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec);
        ~VulkanFramebuffer() override;

        void Bind() override;
        void Unbind() override;
        void Resize(uint32_t width, uint32_t height) override;

        int  ReadPixel(uint32_t attachmentIndex, int x, int y) override;
        void ClearAttachment(uint32_t index, int value) override;

        uint32_t GetColorAttachmentRendererID(uint32_t index = 0) const override;
        uint32_t GetDepthAttachmentRendererID() const override;

        const FramebufferSpecification& GetSpecification() const override { return m_Spec; }

        void BindMSAA() override;
        void BlitMSAA() override;
        bool IsMSAAEnabled() const override { return false; }

        VkRenderPass  GetRenderPass() const { return m_RenderPass; }
        VkFramebuffer GetFramebuffer() const { return m_Framebuffer; }

        // Phase 8.2：供 ImGui 采样与 descriptor 绑定取 attachment 视图
        VkImageView GetColorAttachmentView(uint32_t index = 0) const;
        VkImageView GetDepthAttachmentView() const { return m_DepthAttachment.ImageView; }

    private:
        void Invalidate();
        void Destroy();

    private:
        FramebufferSpecification m_Spec;

        struct AttachmentResource
        {
            VkImage       Image      = VK_NULL_HANDLE;
            VmaAllocation Allocation = nullptr;
            VkImageView   ImageView  = VK_NULL_HANDLE;
            VkFormat      Format     = VK_FORMAT_UNDEFINED;
        };

        std::vector<AttachmentResource>              m_ColorAttachments;
        AttachmentResource                           m_DepthAttachment{};
        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification              m_DepthAttachmentSpec = FramebufferTextureFormat::None;

        VkRenderPass  m_RenderPass  = VK_NULL_HANDLE;
        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    };

} // namespace Engine
