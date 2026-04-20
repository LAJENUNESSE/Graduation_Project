#include "engpch.h"
#include "Platform/Vulkan/VulkanPipeline.h"

#include "Core/Assert.h"

namespace Engine
{

    VkPipeline VulkanPipeline::CreateGraphics(VkDevice device, const VkGraphicsPipelineCreateInfo& createInfo)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "Vulkan device is null");
        ENGINE_CORE_RELEASE_ASSERT(createInfo.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                   "Invalid graphics pipeline create info");
        ENGINE_CORE_RELEASE_ASSERT(createInfo.layout != VK_NULL_HANDLE, "Graphics pipeline layout is null");
        ENGINE_CORE_RELEASE_ASSERT(createInfo.renderPass != VK_NULL_HANDLE, "Graphics pipeline render pass is null");

        VkPipeline     pipeline = VK_NULL_HANDLE;
        const VkResult result   = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan graphics pipeline");

        return pipeline;
    }

} // namespace Engine
