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
        // Framebuffer 抽象层句柄透传（void*，无 vulkan.h 泄漏）
        void* GetColorAttachmentViewHandle(uint32_t index = 0) const override;
        void* GetDepthAttachmentViewHandle() const override;

    private:
        void Invalidate();
        void Destroy();
        // 临时调试：FBO 像素 readback 探针（定位视口黑屏用，验证后移除）
        void DebugRecordPixelReadback(VkCommandBuffer cmd);

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

        // image 创建后 VkImageCreateInfo 的 initialLayout 必须是 UNDEFINED。
        // 首次使用前转到 shader-read-only，确保 render pass 的 initialLayout
        // 与 image 实际布局一致；resize 在当前帧内发生时也能让 ImGui 立即采样新图像。
        bool m_ColorInitialTransitionDone = false;
        bool m_DepthInitialTransitionDone = false;

        // 临时调试探针状态（同上，验证后移除）
        VkBuffer      m_DbgStagingBuffer     = VK_NULL_HANDLE;
        VmaAllocation m_DbgStagingAllocation = nullptr;
        void*         m_DbgStagingMapped     = nullptr;
        VkFence       m_DbgFence             = VK_NULL_HANDLE;
        bool          m_DbgPending           = false;
        uint64_t      m_DbgFrameCounter      = 0;
    };

} // namespace Engine
