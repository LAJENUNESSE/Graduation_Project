#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Engine
{

    class VulkanCommandBuffer
    {
    public:
        explicit VulkanCommandBuffer(VkCommandBuffer commandBuffer = VK_NULL_HANDLE);

        void            SetHandle(VkCommandBuffer commandBuffer);
        VkCommandBuffer GetHandle() const { return m_CommandBuffer; }

        void Reset(VkCommandBufferResetFlags flags = 0) const;
        void Begin(VkCommandBufferUsageFlags usageFlags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) const;
        void End() const;

        // ---- Phase 7 compute 命令封装 ----

        void BindComputePipeline(VkPipeline pipeline) const;

        void BindDescriptorSets(VkPipelineBindPoint                 bindPoint,
                                VkPipelineLayout                    layout,
                                uint32_t                            firstSet,
                                const std::vector<VkDescriptorSet>& sets) const;

        void PushConstants(VkPipelineLayout   layout,
                           VkShaderStageFlags stageFlags,
                           uint32_t           offset,
                           uint32_t           size,
                           const void*        data) const;

        void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) const;

        // 单 buffer barrier
        void BufferBarrier(VkBuffer             buffer,
                           VkDeviceSize         offset,
                           VkDeviceSize         size,
                           VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage,
                           VkAccessFlags        srcAccess,
                           VkAccessFlags        dstAccess) const;

        // 全局内存 barrier（不需指定 buffer/image，覆盖所有资源）
        void MemoryBarrier(VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage,
                           VkAccessFlags        srcAccess,
                           VkAccessFlags        dstAccess) const;

        // 单 image layout transition + barrier
        void ImageBarrier(VkImage              image,
                          VkImageLayout        oldLayout,
                          VkImageLayout        newLayout,
                          VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage,
                          VkAccessFlags        srcAccess,
                          VkAccessFlags        dstAccess,
                          VkImageAspectFlags   aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                          uint32_t             mipLevels  = 1,
                          uint32_t             layers     = 1) const;

    private:
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    };

} // namespace Engine
