#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace Engine
{

    struct VulkanComputePipelineDesc
    {
        VkShaderModule                     ShaderModule = VK_NULL_HANDLE;
        const char*                        EntryPoint   = "main";
        std::vector<VkDescriptorSetLayout> SetLayouts;
        std::vector<VkPushConstantRange>   PushConstants;
    };

    struct VulkanComputePipelineHandle
    {
        VkPipeline       Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout Layout   = VK_NULL_HANDLE;
    };

    class VulkanPipeline
    {
    public:
        static VkPipeline CreateGraphics(VkDevice device, const VkGraphicsPipelineCreateInfo& createInfo);

        // Compute 工厂：根据 desc 创建 VkPipelineLayout + VkPipeline。失败 assert 抛出。
        static VulkanComputePipelineHandle CreateCompute(VkDevice device, const VulkanComputePipelineDesc& desc);

        // 销毁由 CreateCompute 返回的 handle（同时销毁 pipeline 和 layout）
        static void DestroyCompute(VkDevice device, VulkanComputePipelineHandle& handle);
    };

} // namespace Engine
