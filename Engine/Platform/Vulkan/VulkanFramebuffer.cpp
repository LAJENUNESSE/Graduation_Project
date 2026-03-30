#include "engpch.h"
#include "Platform/Vulkan/VulkanFramebuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Core/Log.h"

namespace Engine
{

    uint32_t VulkanFramebuffer::s_NextRendererID = 1;

    // ============================================================================
    // VulkanRenderPass
    // ============================================================================

    VulkanRenderPass::~VulkanRenderPass()
    {
        Destroy();
    }

    void VulkanRenderPass::Create(const std::vector<AttachmentDescription>& attachments,
                                  const std::vector<SubpassDescription>&    subpasses)
    {
        auto* context = VulkanContext::Get();
        if (!context)
        {
            ENGINE_CORE_ERROR("[VulkanRenderPass] No VulkanContext available");
            return;
        }

        VkDevice device = context->GetDevice();

        // Convert attachment descriptions
        std::vector<VkAttachmentDescription> vkAttachments(attachments.size());
        for (size_t i = 0; i < attachments.size(); ++i)
        {
            vkAttachments[i].format         = attachments[i].format;
            vkAttachments[i].samples        = attachments[i].samples;
            vkAttachments[i].loadOp         = attachments[i].loadOp;
            vkAttachments[i].storeOp        = attachments[i].storeOp;
            vkAttachments[i].stencilLoadOp  = attachments[i].stencilLoadOp;
            vkAttachments[i].stencilStoreOp = attachments[i].stencilStoreOp;
            vkAttachments[i].initialLayout  = attachments[i].initialLayout;
            vkAttachments[i].finalLayout    = attachments[i].finalLayout;
        }

        // Build subpass descriptions
        std::vector<VkSubpassDescription>                  vkSubpasses(subpasses.size());
        std::vector<std::vector<VkAttachmentReference>>    colorRefs(subpasses.size());
        std::vector<VkAttachmentReference>                 depthRefs(subpasses.size());
        std::vector<std::vector<VkAttachmentReference>>    inputRefs(subpasses.size());
        std::vector<std::vector<VkAttachmentReference>>    resolveRefs(subpasses.size());

        for (size_t i = 0; i < subpasses.size(); ++i)
        {
            const auto& subpass = subpasses[i];

            // Color attachments
            for (uint32_t idx : subpass.colorAttachmentIndices)
            {
                VkAttachmentReference ref{};
                ref.attachment = idx;
                ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorRefs[i].push_back(ref);
            }

            // Depth attachment
            if (subpass.depthAttachmentIndex >= 0)
            {
                depthRefs[i].attachment = static_cast<uint32_t>(subpass.depthAttachmentIndex);
                depthRefs[i].layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }

            // Input attachments
            for (uint32_t idx : subpass.inputAttachmentIndices)
            {
                VkAttachmentReference ref{};
                ref.attachment = idx;
                ref.layout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                inputRefs[i].push_back(ref);
            }

            // Resolve attachments
            for (uint32_t idx : subpass.resolveAttachmentIndices)
            {
                VkAttachmentReference ref{};
                ref.attachment = idx;
                ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                resolveRefs[i].push_back(ref);
            }

            vkSubpasses[i].pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
            vkSubpasses[i].colorAttachmentCount    = static_cast<uint32_t>(colorRefs[i].size());
            vkSubpasses[i].pColorAttachments       = colorRefs[i].empty() ? nullptr : colorRefs[i].data();
            vkSubpasses[i].pDepthStencilAttachment = subpass.depthAttachmentIndex >= 0 ? &depthRefs[i] : nullptr;
            vkSubpasses[i].inputAttachmentCount    = static_cast<uint32_t>(inputRefs[i].size());
            vkSubpasses[i].pInputAttachments       = inputRefs[i].empty() ? nullptr : inputRefs[i].data();
            vkSubpasses[i].pResolveAttachments     = resolveRefs[i].empty() ? nullptr : resolveRefs[i].data();
        }

        // Subpass dependencies (basic external -> first subpass dependency)
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(vkAttachments.size());
        renderPassInfo.pAttachments    = vkAttachments.data();
        renderPassInfo.subpassCount    = static_cast<uint32_t>(vkSubpasses.size());
        renderPassInfo.pSubpasses      = vkSubpasses.data();
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        VkResult result = vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_RenderPass);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanRenderPass] Failed to create render pass: {}", static_cast<int>(result));
        }
    }

    void VulkanRenderPass::Destroy()
    {
        if (m_RenderPass != VK_NULL_HANDLE)
        {
            auto* context = VulkanContext::Get();
            if (context)
            {
                vkDestroyRenderPass(context->GetDevice(), m_RenderPass, nullptr);
            }
            m_RenderPass = VK_NULL_HANDLE;
        }
    }

    VulkanRenderPass VulkanRenderPass::CreateSimpleColor(VkFormat colorFormat, VkSampleCountFlagBits samples)
    {
        VulkanRenderPass renderPass;

        AttachmentDescription colorAttachment{};
        colorAttachment.format        = colorFormat;
        colorAttachment.samples       = samples;
        colorAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        SubpassDescription subpass{};
        subpass.colorAttachmentIndices = {0};

        renderPass.Create({colorAttachment}, {subpass});
        return renderPass;
    }

    VulkanRenderPass VulkanRenderPass::CreateColorDepth(VkFormat colorFormat, VkFormat depthFormat,
                                                        VkSampleCountFlagBits samples)
    {
        VulkanRenderPass renderPass;

        AttachmentDescription colorAttachment{};
        colorAttachment.format        = colorFormat;
        colorAttachment.samples       = samples;
        colorAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        AttachmentDescription depthAttachment{};
        depthAttachment.format        = depthFormat;
        depthAttachment.samples       = samples;
        depthAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        SubpassDescription subpass{};
        subpass.colorAttachmentIndices = {0};
        subpass.depthAttachmentIndex   = 1;

        renderPass.Create({colorAttachment, depthAttachment}, {subpass});
        return renderPass;
    }

    VulkanRenderPass VulkanRenderPass::CreateSwapchainPresent(VkFormat colorFormat)
    {
        VulkanRenderPass renderPass;

        AttachmentDescription colorAttachment{};
        colorAttachment.format        = colorFormat;
        colorAttachment.samples       = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        SubpassDescription subpass{};
        subpass.colorAttachmentIndices = {0};

        renderPass.Create({colorAttachment}, {subpass});
        return renderPass;
    }

    VulkanRenderPass VulkanRenderPass::CreateDepthOnly(VkFormat depthFormat)
    {
        VulkanRenderPass renderPass;

        AttachmentDescription depthAttachment{};
        depthAttachment.format        = depthFormat;
        depthAttachment.samples       = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        SubpassDescription subpass{};
        subpass.depthAttachmentIndex = 0;

        renderPass.Create({depthAttachment}, {subpass});
        return renderPass;
    }

    // ============================================================================
    // VulkanFramebuffer
    // ============================================================================

    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec) : m_Specification(spec)
    {
        // Separate color and depth attachments
        for (const auto& attachment : spec.Attachments.Attachments)
        {
            if (IsDepthFormat(attachment.TextureFormat))
            {
                m_DepthAttachmentSpecification = attachment;
            }
            else
            {
                m_ColorAttachmentSpecifications.push_back(attachment);
            }
        }

        Invalidate();
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        Cleanup();
    }

    void VulkanFramebuffer::Bind()
    {
        // In Vulkan, "binding" a framebuffer happens via vkCmdBeginRenderPass
        // This method exists for API compatibility
        // Actual render pass begin should use BeginRenderPass()
    }

    void VulkanFramebuffer::Unbind()
    {
        // In Vulkan, "unbinding" happens via vkCmdEndRenderPass
        // This method exists for API compatibility
    }

    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || width > 8192 || height > 8192)
        {
            ENGINE_CORE_WARN("[VulkanFramebuffer] Invalid resize dimensions: {}x{}", width, height);
            return;
        }

        m_Specification.Width  = width;
        m_Specification.Height = height;

        Invalidate();
    }

    int VulkanFramebuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
    {
        // Reading pixels in Vulkan requires staging buffer + memory mapping
        // This is more complex than OpenGL's glReadPixels
        // For now, return 0 - full implementation would need staging buffer transfer
        ENGINE_CORE_WARN("[VulkanFramebuffer] ReadPixel not fully implemented");
        return 0;
    }

    void VulkanFramebuffer::ClearAttachment(uint32_t index, int value)
    {
        // In Vulkan, clearing is done via VkClearValue in vkCmdBeginRenderPass
        // or via vkCmdClearColorImage/vkCmdClearDepthStencilImage
        ENGINE_CORE_WARN("[VulkanFramebuffer] ClearAttachment should use render pass clear values");
    }

    uint32_t VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        if (index < m_ColorAttachments.size())
        {
            return m_ColorAttachments[index].rendererID;
        }
        return 0;
    }

    uint32_t VulkanFramebuffer::GetDepthAttachmentRendererID() const
    {
        return m_DepthAttachment.rendererID;
    }

    VkImageView VulkanFramebuffer::GetColorImageView(uint32_t index) const
    {
        if (index < m_ColorAttachments.size())
        {
            return m_ColorAttachments[index].view;
        }
        return VK_NULL_HANDLE;
    }

    VkImageView VulkanFramebuffer::GetDepthImageView() const
    {
        return m_DepthAttachment.view;
    }

    VkImage VulkanFramebuffer::GetColorImage(uint32_t index) const
    {
        if (index < m_ColorAttachments.size())
        {
            return m_ColorAttachments[index].image;
        }
        return VK_NULL_HANDLE;
    }

    VkImage VulkanFramebuffer::GetDepthImage() const
    {
        return m_DepthAttachment.image;
    }

    void VulkanFramebuffer::BindMSAA()
    {
        // MSAA handling in Vulkan is done via render pass resolve attachments
    }

    void VulkanFramebuffer::BlitMSAA()
    {
        // MSAA resolve happens automatically via render pass in Vulkan
    }

    void VulkanFramebuffer::BeginRenderPass(VkCommandBuffer commandBuffer, const VkClearValue* clearValues,
                                            uint32_t clearValueCount)
    {
        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass        = m_RenderPass.GetRenderPass();
        beginInfo.framebuffer       = m_Framebuffer;
        beginInfo.renderArea.offset = {0, 0};
        beginInfo.renderArea.extent = {m_Specification.Width, m_Specification.Height};

        // Default clear values if none provided
        std::vector<VkClearValue> defaultClearValues;
        if (clearValues == nullptr)
        {
            for (size_t i = 0; i < m_ColorAttachments.size(); ++i)
            {
                VkClearValue clearValue{};
                clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
                defaultClearValues.push_back(clearValue);
            }
            if (m_DepthAttachment.image != VK_NULL_HANDLE)
            {
                VkClearValue clearValue{};
                clearValue.depthStencil = {1.0f, 0};
                defaultClearValues.push_back(clearValue);
            }
            beginInfo.clearValueCount = static_cast<uint32_t>(defaultClearValues.size());
            beginInfo.pClearValues    = defaultClearValues.data();
        }
        else
        {
            beginInfo.clearValueCount = clearValueCount;
            beginInfo.pClearValues    = clearValues;
        }

        vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Set viewport and scissor
        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(m_Specification.Width);
        viewport.height   = static_cast<float>(m_Specification.Height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {m_Specification.Width, m_Specification.Height};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void VulkanFramebuffer::EndRenderPass(VkCommandBuffer commandBuffer)
    {
        vkCmdEndRenderPass(commandBuffer);
    }

    void VulkanFramebuffer::Invalidate()
    {
        Cleanup();

        auto* context = VulkanContext::Get();
        if (!context)
        {
            ENGINE_CORE_ERROR("[VulkanFramebuffer] No VulkanContext available");
            return;
        }

        VkDevice device = context->GetDevice();

        // Create color attachments
        for (const auto& spec : m_ColorAttachmentSpecifications)
        {
            Attachment attachment{};
            CreateAttachment(attachment, spec.TextureFormat,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);
            m_ColorAttachments.push_back(attachment);
        }

        // Create depth attachment
        if (m_DepthAttachmentSpecification.TextureFormat != FramebufferTextureFormat::None)
        {
            VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (m_DepthAttachmentSpecification.TextureFormat == FramebufferTextureFormat::DEPTH_COMPONENT)
            {
                depthUsage |= VK_IMAGE_USAGE_SAMPLED_BIT; // For shadow maps
            }
            CreateAttachment(m_DepthAttachment, m_DepthAttachmentSpecification.TextureFormat, depthUsage,
                             VK_IMAGE_ASPECT_DEPTH_BIT);
        }

        // Build render pass attachment descriptions
        std::vector<VulkanRenderPass::AttachmentDescription> attachmentDescs;
        for (const auto& attachment : m_ColorAttachments)
        {
            VulkanRenderPass::AttachmentDescription desc{};
            desc.format       = attachment.format;
            desc.samples      = VK_SAMPLE_COUNT_1_BIT; // TODO: MSAA support
            desc.loadOp       = VK_ATTACHMENT_LOAD_OP_CLEAR;
            desc.storeOp      = VK_ATTACHMENT_STORE_OP_STORE;
            desc.finalLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachmentDescs.push_back(desc);
        }

        VulkanRenderPass::SubpassDescription subpass{};
        for (uint32_t i = 0; i < m_ColorAttachments.size(); ++i)
        {
            subpass.colorAttachmentIndices.push_back(i);
        }

        if (m_DepthAttachment.image != VK_NULL_HANDLE)
        {
            VulkanRenderPass::AttachmentDescription depthDesc{};
            depthDesc.format       = m_DepthAttachment.format;
            depthDesc.samples      = VK_SAMPLE_COUNT_1_BIT;
            depthDesc.loadOp       = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthDesc.storeOp      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthDesc.finalLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachmentDescs.push_back(depthDesc);
            subpass.depthAttachmentIndex = static_cast<int32_t>(attachmentDescs.size() - 1);
        }

        m_RenderPass.Create(attachmentDescs, {subpass});

        // Collect image views for framebuffer
        std::vector<VkImageView> attachmentViews;
        for (const auto& attachment : m_ColorAttachments)
        {
            attachmentViews.push_back(attachment.view);
        }
        if (m_DepthAttachment.view != VK_NULL_HANDLE)
        {
            attachmentViews.push_back(m_DepthAttachment.view);
        }

        // Create framebuffer
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = m_RenderPass.GetRenderPass();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
        framebufferInfo.pAttachments    = attachmentViews.data();
        framebufferInfo.width           = m_Specification.Width;
        framebufferInfo.height          = m_Specification.Height;
        framebufferInfo.layers          = 1;

        VkResult result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_Framebuffer);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanFramebuffer] Failed to create framebuffer: {}", static_cast<int>(result));
        }
        else
        {
            ENGINE_CORE_TRACE("[VulkanFramebuffer] Created framebuffer {}x{} with {} color attachments",
                              m_Specification.Width, m_Specification.Height, m_ColorAttachments.size());
        }
    }

    void VulkanFramebuffer::Cleanup()
    {
        auto* context = VulkanContext::Get();
        if (!context) return;

        VkDevice device = context->GetDevice();
        vkDeviceWaitIdle(device);

        if (m_Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device, m_Framebuffer, nullptr);
            m_Framebuffer = VK_NULL_HANDLE;
        }

        m_RenderPass.Destroy();

        for (auto& attachment : m_ColorAttachments)
        {
            DestroyAttachment(attachment);
        }
        m_ColorAttachments.clear();

        for (auto& attachment : m_ResolveAttachments)
        {
            DestroyAttachment(attachment);
        }
        m_ResolveAttachments.clear();

        DestroyAttachment(m_DepthAttachment);
    }

    VkFormat VulkanFramebuffer::GetVulkanFormat(FramebufferTextureFormat format) const
    {
        switch (format)
        {
            case FramebufferTextureFormat::RGBA8:           return VK_FORMAT_R8G8B8A8_UNORM;
            case FramebufferTextureFormat::RGBA16F:         return VK_FORMAT_R16G16B16A16_SFLOAT;
            case FramebufferTextureFormat::RED_INTEGER:     return VK_FORMAT_R32_SINT;
            case FramebufferTextureFormat::R32F:            return VK_FORMAT_R32_SFLOAT;
            case FramebufferTextureFormat::R16F:            return VK_FORMAT_R16_SFLOAT;
            case FramebufferTextureFormat::DEPTH24STENCIL8: return VK_FORMAT_D24_UNORM_S8_UINT;
            case FramebufferTextureFormat::DEPTH_COMPONENT: return VK_FORMAT_D32_SFLOAT;
            default:                                        return VK_FORMAT_UNDEFINED;
        }
    }

    bool VulkanFramebuffer::IsDepthFormat(FramebufferTextureFormat format) const
    {
        switch (format)
        {
            case FramebufferTextureFormat::DEPTH24STENCIL8:
            case FramebufferTextureFormat::DEPTH_COMPONENT: return true;
            default:                                        return false;
        }
    }

    void VulkanFramebuffer::CreateAttachment(Attachment& attachment, FramebufferTextureFormat format,
                                             VkImageUsageFlags usage, VkImageAspectFlags aspectFlags)
    {
        auto* context = VulkanContext::Get();
        if (!context) return;

        VkDevice         device         = context->GetDevice();
        VkPhysicalDevice physicalDevice = context->GetPhysicalDevice();

        attachment.format     = GetVulkanFormat(format);
        attachment.rendererID = s_NextRendererID++;

        // Create image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.format        = attachment.format;
        imageInfo.extent.width  = m_Specification.Width;
        imageInfo.extent.height = m_Specification.Height;
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT; // TODO: MSAA
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage         = usage;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult result = vkCreateImage(device, &imageInfo, nullptr, &attachment.image);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanFramebuffer] Failed to create attachment image");
            return;
        }

        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, attachment.image, &memRequirements);

        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        uint32_t memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
        {
            if ((memRequirements.memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memoryTypeIndex = i;
                break;
            }
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        result = vkAllocateMemory(device, &allocInfo, nullptr, &attachment.memory);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanFramebuffer] Failed to allocate attachment memory");
            return;
        }

        vkBindImageMemory(device, attachment.image, attachment.memory, 0);

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = attachment.image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = attachment.format;
        viewInfo.subresourceRange.aspectMask     = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        result = vkCreateImageView(device, &viewInfo, nullptr, &attachment.view);
        if (result != VK_SUCCESS)
        {
            ENGINE_CORE_ERROR("[VulkanFramebuffer] Failed to create attachment image view");
        }
    }

    void VulkanFramebuffer::DestroyAttachment(Attachment& attachment)
    {
        auto* context = VulkanContext::Get();
        if (!context) return;

        VkDevice device = context->GetDevice();

        if (attachment.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, attachment.view, nullptr);
            attachment.view = VK_NULL_HANDLE;
        }
        if (attachment.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, attachment.image, nullptr);
            attachment.image = VK_NULL_HANDLE;
        }
        if (attachment.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, attachment.memory, nullptr);
            attachment.memory = VK_NULL_HANDLE;
        }
        attachment.rendererID = 0;
    }

} // namespace Engine
