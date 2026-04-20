#pragma once

#include <vulkan/vulkan.h>

namespace Engine
{

    class VulkanPipeline
    {
    public:
        static VkPipeline CreateGraphics(VkDevice device, const VkGraphicsPipelineCreateInfo& createInfo);
    };

} // namespace Engine
