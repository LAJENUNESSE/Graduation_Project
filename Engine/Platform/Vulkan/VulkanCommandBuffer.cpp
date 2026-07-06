#include "engpch.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"

#include "Core/Assert.h"

namespace Engine
{

    VulkanCommandBuffer::VulkanCommandBuffer(VkCommandBuffer commandBuffer) : m_CommandBuffer(commandBuffer) {}

    void VulkanCommandBuffer::SetHandle(VkCommandBuffer commandBuffer)
    {
        m_CommandBuffer = commandBuffer;
    }

    void VulkanCommandBuffer::Reset(VkCommandBufferResetFlags flags) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");

        const VkResult result = vkResetCommandBuffer(m_CommandBuffer, flags);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to reset Vulkan command buffer");
    }

    void VulkanCommandBuffer::Begin(VkCommandBufferUsageFlags usageFlags) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = usageFlags;

        const VkResult result = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to begin Vulkan command buffer recording");
    }

    void VulkanCommandBuffer::End() const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");

        const VkResult result = vkEndCommandBuffer(m_CommandBuffer);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to end Vulkan command buffer recording");
    }

    // ===== Phase 7 compute 命令封装 =====

    void VulkanCommandBuffer::BindComputePipeline(VkPipeline pipeline) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");
        vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    }

    void VulkanCommandBuffer::BindDescriptorSets(VkPipelineBindPoint                 bindPoint,
                                                 VkPipelineLayout                    layout,
                                                 uint32_t                            firstSet,
                                                 const std::vector<VkDescriptorSet>& sets) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");
        if (sets.empty())
            return;
        vkCmdBindDescriptorSets(m_CommandBuffer, bindPoint, layout, firstSet, static_cast<uint32_t>(sets.size()),
                                sets.data(), 0, nullptr);
    }

    void VulkanCommandBuffer::PushConstants(
        VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* data) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");
        vkCmdPushConstants(m_CommandBuffer, layout, stageFlags, offset, size, data);
    }

    void VulkanCommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");
        if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
            return;
        vkCmdDispatch(m_CommandBuffer, groupCountX, groupCountY, groupCountZ);
    }

    void VulkanCommandBuffer::BufferBarrier(VkBuffer             buffer,
                                            VkDeviceSize         offset,
                                            VkDeviceSize         size,
                                            VkPipelineStageFlags srcStage,
                                            VkPipelineStageFlags dstStage,
                                            VkAccessFlags        srcAccess,
                                            VkAccessFlags        dstAccess) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");

        VkBufferMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        b.srcAccessMask       = srcAccess;
        b.dstAccessMask       = dstAccess;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer              = buffer;
        b.offset              = offset;
        b.size                = size;

        vkCmdPipelineBarrier(m_CommandBuffer, srcStage, dstStage, 0, 0, nullptr, 1, &b, 0, nullptr);
    }

    void VulkanCommandBuffer::MemoryBarrier(VkPipelineStageFlags srcStage,
                                            VkPipelineStageFlags dstStage,
                                            VkAccessFlags        srcAccess,
                                            VkAccessFlags        dstAccess) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");

        VkMemoryBarrier b{};
        b.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;

        vkCmdPipelineBarrier(m_CommandBuffer, srcStage, dstStage, 0, 1, &b, 0, nullptr, 0, nullptr);
    }

    void VulkanCommandBuffer::ImageBarrier(VkImage              image,
                                           VkImageLayout        oldLayout,
                                           VkImageLayout        newLayout,
                                           VkPipelineStageFlags srcStage,
                                           VkPipelineStageFlags dstStage,
                                           VkAccessFlags        srcAccess,
                                           VkAccessFlags        dstAccess,
                                           VkImageAspectFlags   aspectMask,
                                           uint32_t             mipLevels,
                                           uint32_t             layers) const
    {
        ENGINE_CORE_RELEASE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "VulkanCommandBuffer handle is null");

        VkImageMemoryBarrier b{};
        b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                       = oldLayout;
        b.newLayout                       = newLayout;
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = image;
        b.subresourceRange.aspectMask     = aspectMask;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = mipLevels;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = layers;
        b.srcAccessMask                   = srcAccess;
        b.dstAccessMask                   = dstAccess;

        vkCmdPipelineBarrier(m_CommandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

} // namespace Engine
