#include "engpch.h"
#include "VulkanCommandBuffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"

namespace Engine
{

    VulkanCommandBuffer::VulkanCommandBuffer(VkDevice device, uint32_t queueFamilyIndex) : m_Device(device)
    {
        // Create command pool
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkResult result = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create command pool!");

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_CommandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(m_Device, &allocInfo, &m_CommandBuffer);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to allocate command buffer!");
    }

    VulkanCommandBuffer::~VulkanCommandBuffer()
    {
        if (m_CommandBuffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &m_CommandBuffer);
        }
        if (m_CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        }
    }

    void VulkanCommandBuffer::Begin()
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;

        VkResult result = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to begin command buffer!");
    }

    void VulkanCommandBuffer::End()
    {
        VkResult result = vkEndCommandBuffer(m_CommandBuffer);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to end command buffer!");
    }

    void VulkanCommandBuffer::Submit(VkQueue queue, VkSemaphore waitSemaphore, VkSemaphore signalSemaphore,
                                     VkFence fence)
    {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        if (waitSemaphore != VK_NULL_HANDLE)
        {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores    = &waitSemaphore;
            submitInfo.pWaitDstStageMask  = waitStages;
        }

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &m_CommandBuffer;

        if (signalSemaphore != VK_NULL_HANDLE)
        {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = &signalSemaphore;
        }

        VkResult result = vkQueueSubmit(queue, 1, &submitInfo, fence);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to submit command buffer!");
    }

    VkCommandBuffer VulkanCommandBuffer::BeginSingleTime(VkDevice device, VkCommandPool pool)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = pool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void VulkanCommandBuffer::EndSingleTime(VkDevice device, VkCommandPool pool, VkQueue queue,
                                            VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue); // Wait for completion

        vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
    }

} // namespace Engine
