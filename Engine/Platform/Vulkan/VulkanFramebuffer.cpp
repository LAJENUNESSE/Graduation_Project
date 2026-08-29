#include "engpch.h"
#include "Platform/Vulkan/VulkanFramebuffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanAllocator.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"

#include <vma/vk_mem_alloc.h>

namespace Engine
{
    static const uint32_t s_MaxFramebufferSize = 8192;

    namespace Utils
    {
        static bool IsDepthFormat(FramebufferTextureFormat format)
        {
            switch (format)
            {
            case FramebufferTextureFormat::DEPTH24STENCIL8:
            case FramebufferTextureFormat::DEPTH_COMPONENT:
                return true;
            default:
                return false;
            }
        }

        static VkFormat FBTextureFormatToVk(FramebufferTextureFormat format)
        {
            switch (format)
            {
            case FramebufferTextureFormat::RGBA8:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case FramebufferTextureFormat::RGBA16F:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case FramebufferTextureFormat::RED_INTEGER:
                return VK_FORMAT_R32_SINT;
            case FramebufferTextureFormat::R32F:
                return VK_FORMAT_R32_SFLOAT;
            case FramebufferTextureFormat::R16F:
                return VK_FORMAT_R16_SFLOAT;
            case FramebufferTextureFormat::DEPTH24STENCIL8:
                return VK_FORMAT_D24_UNORM_S8_UINT;
            case FramebufferTextureFormat::DEPTH_COMPONENT:
                return VK_FORMAT_D32_SFLOAT;
            default:
                return VK_FORMAT_UNDEFINED;
            }
        }

        static VkImageUsageFlags ColorUsageFlags()
        {
            return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        static VkImageUsageFlags DepthUsageFlags()
        {
            return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        static VkImageAspectFlags AspectForFormat(VkFormat format)
        {
            if (format == VK_FORMAT_D24_UNORM_S8_UINT)
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM)
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    } // namespace Utils

    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec) : m_Spec(spec)
    {
        for (auto& att : m_Spec.Attachments.Attachments)
        {
            if (Utils::IsDepthFormat(att.TextureFormat))
                m_DepthAttachmentSpec = att;
            else
                m_ColorAttachmentSpecs.emplace_back(att);
        }

        Invalidate();
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        Destroy();
    }

    void VulkanFramebuffer::Invalidate()
    {
        Destroy();

        auto* context = VulkanContext::Get();
        if (!context)
            return;

        VkDevice device = context->GetDevice();

        m_ColorInitialTransitionDone = false;
        m_DepthInitialTransitionDone = false;

        if (m_Spec.Width == 0 || m_Spec.Height == 0)
        {
            static bool s_WarnedZeroSize = false;
            if (!s_WarnedZeroSize)
            {
                s_WarnedZeroSize = true;
                ENGINE_CORE_WARN("[VulkanFramebuffer] Invalidate skipped: size=({0},{1})", m_Spec.Width, m_Spec.Height);
            }
            return;
        }
        ENGINE_CORE_WARN("[VulkanFramebuffer] Invalidate size=({0},{1})", m_Spec.Width, m_Spec.Height);

        // --- Create color attachment images ---
        m_ColorAttachments.resize(m_ColorAttachmentSpecs.size());
        for (size_t i = 0; i < m_ColorAttachmentSpecs.size(); i++)
        {
            auto& res  = m_ColorAttachments[i];
            res.Format = Utils::FBTextureFormatToVk(m_ColorAttachmentSpecs[i].TextureFormat);

            VkImageCreateInfo imageInfo{};
            imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.format        = res.Format;
            imageInfo.extent        = {m_Spec.Width, m_Spec.Height, 1};
            imageInfo.mipLevels     = 1;
            imageInfo.arrayLayers   = 1;
            imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage         = Utils::ColorUsageFlags();
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

            VkResult result = vmaCreateImage(VulkanAllocator::GetAllocator(), &imageInfo, &allocInfo, &res.Image,
                                             &res.Allocation, nullptr);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan framebuffer color image");

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image                           = res.Image;
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = res.Format;
            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            result = vkCreateImageView(device, &viewInfo, nullptr, &res.ImageView);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan framebuffer color image view");
        }

        // --- Create depth attachment image ---
        bool hasDepth = m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None;
        if (hasDepth)
        {
            m_DepthAttachment.Format = Utils::FBTextureFormatToVk(m_DepthAttachmentSpec.TextureFormat);

            VkImageCreateInfo imageInfo{};
            imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.format        = m_DepthAttachment.Format;
            imageInfo.extent        = {m_Spec.Width, m_Spec.Height, 1};
            imageInfo.mipLevels     = 1;
            imageInfo.arrayLayers   = 1;
            imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage         = Utils::DepthUsageFlags();
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

            VkResult result = vmaCreateImage(VulkanAllocator::GetAllocator(), &imageInfo, &allocInfo,
                                             &m_DepthAttachment.Image, &m_DepthAttachment.Allocation, nullptr);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan framebuffer depth image");

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image                           = m_DepthAttachment.Image;
            viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format                          = m_DepthAttachment.Format;
            viewInfo.subresourceRange.aspectMask     = Utils::AspectForFormat(m_DepthAttachment.Format);
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            result = vkCreateImageView(device, &viewInfo, nullptr, &m_DepthAttachment.ImageView);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan framebuffer depth image view");
        }

        // --- Create render pass ---
        std::vector<VkAttachmentDescription> attachmentDescs;
        std::vector<VkAttachmentReference>   colorRefs;

        for (size_t i = 0; i < m_ColorAttachments.size(); i++)
        {
            VkAttachmentDescription desc{};
            desc.format  = m_ColorAttachments[i].Format;
            desc.samples = VK_SAMPLE_COUNT_1_BIT;
            // GL 语义对齐：FBO 一帧内会被多个 pass 反复 Bind（几何/粒子/调试叠加），
            // 清屏由上层 RenderCommand::Clear()（vkCmdClearAttachments）显式完成；
            // 这里用 LOAD 保证后续 Bind 不抹掉此前 pass 的绘制结果。
            desc.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
            desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachmentDescs.push_back(desc);

            VkAttachmentReference ref{};
            ref.attachment = static_cast<uint32_t>(i);
            ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs.push_back(ref);
        }

        VkAttachmentReference depthRef{};
        if (hasDepth)
        {
            VkAttachmentDescription desc{};
            desc.format  = m_DepthAttachment.Format;
            desc.samples = VK_SAMPLE_COUNT_1_BIT;
            // 同颜色附件：深度清理由上层 Clear() 显式完成，各 pass 间保留深度做遮挡测试。
            desc.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
            desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            // depth attachment 退出 render pass 自动 transition 到 finalLayout。
            // Vulkan 规范禁止 depth finalLayout 用 SHADER_READ_ONLY_OPTIMAL（不在
            // 允许的 depth 转换目标列表里），必须用 DEPTH_STENCIL_READ_ONLY_OPTIMAL。
            // 配套的 descriptor 写入也用 DEPTH_STENCIL_READ_ONLY_OPTIMAL（在 SceneDrawDispatcher
            // 通过 Format 检测 depth view 后选用此 layout）。
            desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            attachmentDescs.push_back(desc);

            depthRef.attachment = static_cast<uint32_t>(attachmentDescs.size() - 1);
            depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = static_cast<uint32_t>(colorRefs.size());
        subpass.pColorAttachments       = colorRefs.empty() ? nullptr : colorRefs.data();
        subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        // 场景 FBO 在同一条主命令缓冲中会连续经历：
        //   上一个 pass 写入 -> 下一个 pass 采样 -> 本 pass 再写入。
        // 仅依赖布局转换不足以建立 shader-read 的可见性，尤其是 HDR -> tone map
        // 和 targetFBO -> ImGui 这两条链。显式声明双向 external 依赖，避免采样到
        // 未完成的颜色/深度写入。
        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass   = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass   = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
        renderPassInfo.pAttachments    = attachmentDescs.empty() ? nullptr : attachmentDescs.data();
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies   = dependencies.data();

        VkResult result = vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_RenderPass);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan framebuffer render pass");

        // --- Create framebuffer ---
        std::vector<VkImageView> attachmentViews;
        for (auto& att : m_ColorAttachments)
            attachmentViews.push_back(att.ImageView);
        if (hasDepth)
            attachmentViews.push_back(m_DepthAttachment.ImageView);

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_RenderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
        fbInfo.pAttachments    = attachmentViews.empty() ? nullptr : attachmentViews.data();
        fbInfo.width           = m_Spec.Width;
        fbInfo.height          = m_Spec.Height;
        fbInfo.layers          = 1;

        result = vkCreateFramebuffer(device, &fbInfo, nullptr, &m_Framebuffer);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan framebuffer");

        // ---- 一次性 transition 新 attachment：UNDEFINED → shader-read-only ----
        // resize 可能发生在当前帧 render pass 之后、ImGui 采样之前；用独立的一次性
        // command buffer 立即提交，避免新 view 在同一帧被采样时仍停留在 UNDEFINED。
        {
            const VkCommandBuffer transitionCmd = context->BeginSingleTimeCommands();
            VulkanCommandBuffer   transitionBuffer(transitionCmd);
            for (const auto& attachment : m_ColorAttachments)
            {
                transitionBuffer.ImageBarrier(attachment.Image, VK_IMAGE_LAYOUT_UNDEFINED,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                              0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            }
            m_ColorInitialTransitionDone = true;

            if (m_DepthAttachment.Image != VK_NULL_HANDLE &&
                m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None)
            {
                transitionBuffer.ImageBarrier(
                    m_DepthAttachment.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                    VK_ACCESS_SHADER_READ_BIT, Utils::AspectForFormat(m_DepthAttachment.Format));
                m_DepthInitialTransitionDone = true;
            }

            context->EndSingleTimeCommands(transitionCmd);
        }
    }

    void VulkanFramebuffer::Destroy()
    {
        auto* context = VulkanContext::Get();
        if (!context)
            return;

        // attachment 视图即将销毁：注销 ImGui 纹理缓存中的悬垂 descriptor set
        for (const auto& att : m_ColorAttachments)
            context->RemoveImGuiTexture(att.ImageView);
        context->RemoveImGuiTexture(m_DepthAttachment.ImageView);

        // GPU 句柄不能在此同步销毁：本函数会在录制窗口内被调用（场景切换/MSAA 重建），
        // vkDeviceWaitIdle 只能等已提交的工作，保护不了正在录制的命令缓冲。
        // 打包推迟到下一轮 fence 确认后释放（VulkanDeletionQueue）。
        const VkFramebuffer                   framebuffer      = m_Framebuffer;
        const VkRenderPass                    renderPass       = m_RenderPass;
        const std::vector<AttachmentResource> colorAttachments = m_ColorAttachments;
        const AttachmentResource              depthAttachment  = m_DepthAttachment;
        const bool                            allocatorReady   = VulkanAllocator::IsInitialized();

        VulkanContext::DeferDestroy(
            [framebuffer, renderPass, colorAttachments, depthAttachment, allocatorReady](VkDevice device)
            {
                if (framebuffer != VK_NULL_HANDLE)
                    vkDestroyFramebuffer(device, framebuffer, nullptr);
                if (renderPass != VK_NULL_HANDLE)
                    vkDestroyRenderPass(device, renderPass, nullptr);

                for (const auto& att : colorAttachments)
                {
                    if (att.ImageView != VK_NULL_HANDLE)
                        vkDestroyImageView(device, att.ImageView, nullptr);
                    if (att.Image != VK_NULL_HANDLE && allocatorReady)
                        vmaDestroyImage(VulkanAllocator::GetAllocator(), att.Image, att.Allocation);
                }

                if (depthAttachment.ImageView != VK_NULL_HANDLE)
                    vkDestroyImageView(device, depthAttachment.ImageView, nullptr);
                if (depthAttachment.Image != VK_NULL_HANDLE && allocatorReady)
                    vmaDestroyImage(VulkanAllocator::GetAllocator(), depthAttachment.Image, depthAttachment.Allocation);
            });

        m_Framebuffer = VK_NULL_HANDLE;
        m_RenderPass  = VK_NULL_HANDLE;
        m_ColorAttachments.clear();
        m_DepthAttachment = {};
    }

    // Phase 8.2：录制场景 render pass 进主帧 cmd。上层 SceneRenderer::RenderPipeline
    // 的 Bind/Unbind 配对调用点已存在；loadOp=CLEAR 已在内部 renderpass 配置，
    // 清屏值在此提供（颜色取 Context 清屏色，深度固定 1.0）。
    void VulkanFramebuffer::Bind()
    {
        auto* context = VulkanContext::Get();
        if (!context)
            return;

        const VkCommandBuffer cmd = context->GetCurrentFrameCommandBuffer();
        if (cmd == VK_NULL_HANDLE || m_Framebuffer == VK_NULL_HANDLE || m_RenderPass == VK_NULL_HANDLE)
            return;

        VulkanCommandBuffer commandBuffer(cmd);
        if (!m_ColorInitialTransitionDone)
        {
            for (const auto& attachment : m_ColorAttachments)
            {
                commandBuffer.ImageBarrier(attachment.Image, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT,
                                           VK_IMAGE_ASPECT_COLOR_BIT);
            }
            m_ColorInitialTransitionDone = true;
        }

        if (!m_DepthInitialTransitionDone && m_DepthAttachment.Image != VK_NULL_HANDLE &&
            m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None)
        {
            commandBuffer.ImageBarrier(m_DepthAttachment.Image, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                       VK_ACCESS_SHADER_READ_BIT, Utils::AspectForFormat(m_DepthAttachment.Format));
            m_DepthInitialTransitionDone = true;
        }

        std::vector<VkClearValue> clearValues;
        const glm::vec4           clearColor = context->GetClearColor();
        for (size_t i = 0; i < m_ColorAttachments.size(); ++i)
        {
            VkClearValue v{};
            v.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
            clearValues.push_back(v);
        }
        if (m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None)
        {
            VkClearValue d{};
            d.depthStencil = {1.0f, 0};
            clearValues.push_back(d);
        }

        commandBuffer.BeginRenderPass(m_RenderPass, m_Framebuffer, {{0, 0}, {m_Spec.Width, m_Spec.Height}},
                                      clearValues);
        commandBuffer.SetViewport(0, 0, static_cast<float>(m_Spec.Width), static_cast<float>(m_Spec.Height));
        commandBuffer.SetScissor(0, 0, m_Spec.Width, m_Spec.Height);

        const bool hasDepth = m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None;
        context->SetActiveSceneRenderPass(m_RenderPass, static_cast<uint32_t>(m_ColorAttachments.size()), hasDepth,
                                          m_Spec.Width, m_Spec.Height);
    }

    void VulkanFramebuffer::Unbind()
    {
        auto* context = VulkanContext::Get();
        if (!context || context->GetActiveSceneRenderPass() != m_RenderPass || m_RenderPass == VK_NULL_HANDLE)
            return;

        const VkCommandBuffer cmd = context->GetCurrentFrameCommandBuffer();
        if (cmd == VK_NULL_HANDLE)
            return;

        VulkanCommandBuffer(cmd).EndRenderPass();
        context->SetActiveSceneRenderPass(VK_NULL_HANDLE, 0, false, 0, 0);
    }

    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || width > s_MaxFramebufferSize || height > s_MaxFramebufferSize)
        {
            ENGINE_CORE_WARN("Attempted to resize Vulkan framebuffer to {0}, {1}", width, height);
            return;
        }

        ENGINE_CORE_WARN("[VulkanFramebuffer::Resize] this={0} size=({1},{2})", static_cast<const void*>(this), width,
                         height);
        m_Spec.Width  = width;
        m_Spec.Height = height;

        Invalidate();
    }

    int VulkanFramebuffer::ReadPixel(uint32_t /*attachmentIndex*/, int /*x*/, int /*y*/)
    {
        return -1;
    }

    void VulkanFramebuffer::ClearAttachment(uint32_t /*index*/, int /*value*/) {}

    uint32_t VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t /*index*/) const
    {
        return 0;
    }

    uint32_t VulkanFramebuffer::GetDepthAttachmentRendererID() const
    {
        return 0;
    }

    void VulkanFramebuffer::BindMSAA() {}

    void VulkanFramebuffer::BlitMSAA() {}

    void* VulkanFramebuffer::GetColorAttachmentViewHandle(uint32_t index) const
    {
        return reinterpret_cast<void*>(GetColorAttachmentView(index));
    }

    void* VulkanFramebuffer::GetDepthAttachmentViewHandle() const
    {
        return reinterpret_cast<void*>(m_DepthAttachment.ImageView);
    }

    VkImageView VulkanFramebuffer::GetColorAttachmentView(uint32_t index) const
    {
        if (index >= m_ColorAttachments.size())
        {
            ENGINE_CORE_WARN("[Vulkan] Color attachment view index {0} out of range ({1})", index,
                             m_ColorAttachments.size());
            return VK_NULL_HANDLE;
        }
        return m_ColorAttachments[index].ImageView;
    }

} // namespace Engine
