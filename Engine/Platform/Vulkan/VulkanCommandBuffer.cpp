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

} // namespace Engine
