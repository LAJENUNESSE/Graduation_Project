#include "engpch.h"
#include "Platform/Vulkan/VulkanPipeline.h"

#include "Core/Assert.h"
#include "Platform/Vulkan/VulkanContext.h"

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

    VulkanComputePipelineHandle VulkanPipeline::CreateCompute(VkDevice device, const VulkanComputePipelineDesc& desc)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "Vulkan device is null");
        ENGINE_CORE_RELEASE_ASSERT(desc.ShaderModule != VK_NULL_HANDLE, "Compute pipeline shader module is null");
        ENGINE_CORE_RELEASE_ASSERT(desc.EntryPoint != nullptr, "Compute pipeline entry point is null");

        VulkanComputePipelineHandle handle{};

        // 1) Pipeline Layout
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = static_cast<uint32_t>(desc.SetLayouts.size());
        layoutInfo.pSetLayouts            = desc.SetLayouts.empty() ? nullptr : desc.SetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(desc.PushConstants.size());
        layoutInfo.pPushConstantRanges    = desc.PushConstants.empty() ? nullptr : desc.PushConstants.data();

        VkResult r = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &handle.Layout);
        ENGINE_CORE_RELEASE_ASSERT(r == VK_SUCCESS, "Failed to create compute pipeline layout");

        // 2) Compute Pipeline
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout       = handle.Layout;
        pipelineInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = desc.ShaderModule;
        pipelineInfo.stage.pName  = desc.EntryPoint;

        r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &handle.Pipeline);
        if (r != VK_SUCCESS)
        {
            vkDestroyPipelineLayout(device, handle.Layout, nullptr);
            handle.Layout = VK_NULL_HANDLE;
            ENGINE_CORE_RELEASE_ASSERT(false, "Failed to create Vulkan compute pipeline");
        }

        return handle;
    }

    void VulkanPipeline::DestroyCompute(VkDevice device, VulkanComputePipelineHandle& handle)
    {
        if (device == VK_NULL_HANDLE)
            return;

        const VkPipeline       pipeline = handle.Pipeline;
        const VkPipelineLayout layout   = handle.Layout;
        handle.Pipeline                 = VK_NULL_HANDLE;
        handle.Layout                   = VK_NULL_HANDLE;

        if (pipeline == VK_NULL_HANDLE && layout == VK_NULL_HANDLE)
            return;

        auto destroy = [pipeline, layout](VkDevice destroyDevice)
        {
            if (pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(destroyDevice, pipeline, nullptr);
            if (layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(destroyDevice, layout, nullptr);
        };

        if (VulkanContext::Get())
        {
            VulkanContext::DeferDestroy(std::move(destroy));
            return;
        }

        destroy(device);
    }

} // namespace Engine
