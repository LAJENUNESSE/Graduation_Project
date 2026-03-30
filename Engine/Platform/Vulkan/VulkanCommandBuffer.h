#pragma once

#include <vulkan/vulkan.h>

namespace Engine
{

    class VulkanCommandBuffer
    {
    public:
        VulkanCommandBuffer(VkDevice device, uint32_t queueFamilyIndex);
        ~VulkanCommandBuffer();

        void Begin();
        void End();
        void Submit(VkQueue queue, VkSemaphore waitSemaphore = VK_NULL_HANDLE,
                    VkSemaphore signalSemaphore = VK_NULL_HANDLE, VkFence fence = VK_NULL_HANDLE);

        VkCommandBuffer GetHandle() const { return m_CommandBuffer; }
        VkCommandPool   GetPool() const { return m_CommandPool; }

        // Single-time command helpers (for resource uploads, transitions, etc.)
        static VkCommandBuffer BeginSingleTime(VkDevice device, VkCommandPool pool);
        static void            EndSingleTime(VkDevice device, VkCommandPool pool, VkQueue queue,
                                             VkCommandBuffer commandBuffer);

    private:
        VkDevice        m_Device;
        VkCommandPool   m_CommandPool   = VK_NULL_HANDLE;
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    };

} // namespace Engine
