#pragma once

#include "Renderer/Framebuffer.h"

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace Engine
{

    // ============================================================================
    // VulkanRenderPass - Encapsulates render pass creation and management
    // ============================================================================
    class VulkanRenderPass
    {
    public:
        struct AttachmentDescription
        {
            VkFormat              format         = VK_FORMAT_UNDEFINED;
            VkSampleCountFlagBits samples        = VK_SAMPLE_COUNT_1_BIT;
            VkAttachmentLoadOp    loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentStoreOp   storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            VkAttachmentLoadOp    stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp   stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkImageLayout         initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout         finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        };

        struct SubpassDescription
        {
            std::vector<uint32_t> colorAttachmentIndices;
            int32_t               depthAttachmentIndex = -1; // -1 means no depth
            std::vector<uint32_t> inputAttachmentIndices;
            std::vector<uint32_t> resolveAttachmentIndices;
        };

        VulkanRenderPass() = default;
        ~VulkanRenderPass();

        void Create(const std::vector<AttachmentDescription>& attachments,
                    const std::vector<SubpassDescription>&    subpasses);
        void Destroy();

        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        bool         IsValid() const { return m_RenderPass != VK_NULL_HANDLE; }

        // Common render pass presets
        static VulkanRenderPass CreateSimpleColor(VkFormat colorFormat, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
        static VulkanRenderPass CreateColorDepth(VkFormat colorFormat, VkFormat depthFormat,
                                                 VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
        static VulkanRenderPass CreateSwapchainPresent(VkFormat colorFormat);
        static VulkanRenderPass CreateDepthOnly(VkFormat depthFormat);

    private:
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    };

    // ============================================================================
    // VulkanFramebuffer - Vulkan implementation of Framebuffer
    // ============================================================================
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

        const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

        // MSAA support
        void BindMSAA() override;
        void BlitMSAA() override;
        bool IsMSAAEnabled() const override { return m_Specification.Samples > 1; }

        // Vulkan-specific accessors
        VkFramebuffer GetFramebuffer() const { return m_Framebuffer; }
        VkRenderPass  GetRenderPass() const { return m_RenderPass.GetRenderPass(); }
        VkImageView   GetColorImageView(uint32_t index = 0) const;
        VkImageView   GetDepthImageView() const;
        VkImage       GetColorImage(uint32_t index = 0) const;
        VkImage       GetDepthImage() const;

        // For rendering
        void         BeginRenderPass(VkCommandBuffer commandBuffer, const VkClearValue* clearValues = nullptr,
                                     uint32_t clearValueCount = 0);
        void         EndRenderPass(VkCommandBuffer commandBuffer);
        VkExtent2D   GetExtent() const { return {m_Specification.Width, m_Specification.Height}; }
        uint32_t     GetColorAttachmentCount() const { return static_cast<uint32_t>(m_ColorAttachments.size()); }

    private:
        void Invalidate();
        void Cleanup();

        VkFormat GetVulkanFormat(FramebufferTextureFormat format) const;
        bool     IsDepthFormat(FramebufferTextureFormat format) const;

        struct Attachment
        {
            VkImage        image      = VK_NULL_HANDLE;
            VkDeviceMemory memory     = VK_NULL_HANDLE;
            VkImageView    view       = VK_NULL_HANDLE;
            VkFormat       format     = VK_FORMAT_UNDEFINED;
            uint32_t       rendererID = 0; // For compatibility with OpenGL-style API
        };

        void CreateAttachment(Attachment& attachment, FramebufferTextureFormat format, VkImageUsageFlags usage,
                              VkImageAspectFlags aspectFlags);
        void DestroyAttachment(Attachment& attachment);

        FramebufferSpecification m_Specification;
        VulkanRenderPass         m_RenderPass;
        VkFramebuffer            m_Framebuffer = VK_NULL_HANDLE;

        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecifications;
        FramebufferTextureSpecification              m_DepthAttachmentSpecification = FramebufferTextureFormat::None;

        std::vector<Attachment> m_ColorAttachments;
        Attachment              m_DepthAttachment;

        // MSAA resolve targets (when MSAA is enabled, we need separate resolve images)
        std::vector<Attachment> m_ResolveAttachments;

        static uint32_t s_NextRendererID;
    };

} // namespace Engine
