#pragma once

#include <vulkan/vulkan.h>

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

    private:
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    };

} // namespace Engine
