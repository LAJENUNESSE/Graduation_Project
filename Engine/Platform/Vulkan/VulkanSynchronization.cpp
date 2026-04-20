#include "engpch.h"
#include "Platform/Vulkan/VulkanSynchronization.h"

#include "Core/Assert.h"

namespace Engine
{

    VulkanFrameSyncObjects VulkanSynchronization::CreateFrameSyncObjects(VkDevice device, bool createSignaledFence)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "Vulkan device is null");

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = createSignaledFence ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

        VulkanFrameSyncObjects syncObjects{};

        const VkResult imageAvailableResult =
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &syncObjects.ImageAvailable);
        const VkResult renderFinishedResult =
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &syncObjects.RenderFinished);
        const VkResult fenceResult = vkCreateFence(device, &fenceInfo, nullptr, &syncObjects.InFlight);

        ENGINE_CORE_RELEASE_ASSERT(imageAvailableResult == VK_SUCCESS && renderFinishedResult == VK_SUCCESS &&
                                       fenceResult == VK_SUCCESS,
                                   "Failed to create Vulkan sync objects");

        return syncObjects;
    }

    void VulkanSynchronization::DestroyFrameSyncObjects(VkDevice device, VulkanFrameSyncObjects& syncObjects)
    {
        if (device == VK_NULL_HANDLE)
            return;

        if (syncObjects.ImageAvailable != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, syncObjects.ImageAvailable, nullptr);
            syncObjects.ImageAvailable = VK_NULL_HANDLE;
        }

        if (syncObjects.RenderFinished != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, syncObjects.RenderFinished, nullptr);
            syncObjects.RenderFinished = VK_NULL_HANDLE;
        }

        if (syncObjects.InFlight != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, syncObjects.InFlight, nullptr);
            syncObjects.InFlight = VK_NULL_HANDLE;
        }
    }

} // namespace Engine
