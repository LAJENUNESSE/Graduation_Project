#pragma once

#include <vulkan/vulkan.h>

namespace Engine
{

    struct VulkanFrameSyncObjects
    {
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        VkSemaphore RenderFinished = VK_NULL_HANDLE;
        VkFence     InFlight       = VK_NULL_HANDLE;
    };

    class VulkanSynchronization
    {
    public:
        static VulkanFrameSyncObjects CreateFrameSyncObjects(VkDevice device, bool createSignaledFence = true);
        static void                   DestroyFrameSyncObjects(VkDevice device, VulkanFrameSyncObjects& syncObjects);
    };

} // namespace Engine
