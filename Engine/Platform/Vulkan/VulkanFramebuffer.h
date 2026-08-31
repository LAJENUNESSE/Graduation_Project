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

        // screen-space 流体链（vkCmdCopyImage sceneColor 拷贝 + composite 采样 sceneDepth）：
        // 拷贝源与采样源需要 VkImage 句柄做 layout 转换；view 供 descriptor 绑定
        VkImage     GetColorAttachmentImage(uint32_t index = 0) const;
        VkFormat    GetColorAttachmentFormat(uint32_t index = 0) const;
        VkImageView GetDepthAttachmentImageView() const { return m_DepthAttachment.ImageView; }
        VkImage     GetDepthAttachmentImage() const { return m_DepthAttachment.Image; }
        VkFormat    GetDepthAttachmentFormat() const { return m_DepthAttachment.Format; }
        // depth-only aspect 采样 view：sampler2D 采样 D24S8 时 descriptor view 不得含
        // STENCIL aspect（VUID-VkDescriptorImageInfo-imageView-01976）。懒创建，析构销毁
        VkImageView GetDepthAttachmentSampledView();
        void*       GetDepthAttachmentSampledViewHandle();

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

        std::vector<AttachmentResource> m_ColorAttachments;
        AttachmentResource              m_DepthAttachment{};
        // depth-only 采样 view（懒创建）
        VkImageView m_DepthSampledView = VK_NULL_HANDLE;

        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification              m_DepthAttachmentSpec = FramebufferTextureFormat::None;

        VkRenderPass  m_RenderPass  = VK_NULL_HANDLE;
        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;

        // image 创建后 VkImageCreateInfo 的 initialLayout 必须是 UNDEFINED。
        // 首次使用前转到 shader-read-only，确保 render pass 的 initialLayout
        // 与 image 实际布局一致；resize 在当前帧内发生时也能让 ImGui 立即采样新图像。
        bool m_ColorInitialTransitionDone = false;
        bool m_DepthInitialTransitionDone = false;

        // ReadPixel 同步回读用 4B staging（persistently mapped，FBO 生命周期内复用）
        VkBuffer      m_ReadbackBuffer     = VK_NULL_HANDLE;
        VmaAllocation m_ReadbackAllocation = nullptr;
        void*         m_ReadbackMapped     = nullptr;
    };

} // namespace Engine
