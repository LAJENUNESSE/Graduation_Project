#include "engpch.h"
#include "Platform/Vulkan/VulkanRenderPass.h"

#include "Core/Assert.h"

namespace Engine
{

    VkRenderPass VulkanRenderPass::CreateColorOnly(VkDevice device, const VulkanColorRenderPassDesc& desc)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "Vulkan device is null");
        ENGINE_CORE_RELEASE_ASSERT(desc.ColorFormat != VK_FORMAT_UNDEFINED,
                                   "Color format must be valid for Vulkan render pass creation");

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = desc.ColorFormat;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = desc.LoadOp;
        colorAttachment.storeOp        = desc.StoreOp;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = desc.InitialLayout;
        colorAttachment.finalLayout    = desc.FinalLayout;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &colorAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        VkRenderPass   renderPass = VK_NULL_HANDLE;
        const VkResult result     = vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan color render pass");

        return renderPass;
    }

} // namespace Engine
