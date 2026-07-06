#pragma once

#include <vulkan/vulkan.h>

namespace Engine
{

    struct VulkanColorRenderPassDesc
    {
        VkFormat            ColorFormat   = VK_FORMAT_UNDEFINED;
        VkAttachmentLoadOp  LoadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp StoreOp       = VK_ATTACHMENT_STORE_OP_STORE;
        VkImageLayout       InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout       FinalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    };

    class VulkanRenderPass
    {
    public:
        static VkRenderPass CreateColorOnly(VkDevice device, const VulkanColorRenderPassDesc& desc);
    };

} // namespace Engine
