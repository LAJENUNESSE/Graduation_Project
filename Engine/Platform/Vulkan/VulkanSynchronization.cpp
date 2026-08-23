#include "engpch.h"
#include "Platform/Vulkan/VulkanSynchronization.h"

#include "Core/Assert.h"

namespace Engine
{

    VkFence VulkanSynchronization::CreateFence(VkDevice device, bool createSignaled)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "Vulkan device is null");

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = createSignaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

        VkFence fence = VK_NULL_HANDLE;
        ENGINE_CORE_RELEASE_ASSERT(vkCreateFence(device, &fenceInfo, nullptr, &fence) == VK_SUCCESS,
                                   "Failed to create Vulkan fence");
        return fence;
    }

    VkSemaphore VulkanSynchronization::CreateSemaphore(VkDevice device)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "Vulkan device is null");

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkSemaphore semaphore = VK_NULL_HANDLE;
        ENGINE_CORE_RELEASE_ASSERT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) == VK_SUCCESS,
                                   "Failed to create Vulkan semaphore");
        return semaphore;
    }

    void VulkanSynchronization::DestroyFence(VkDevice device, VkFence& fence)
    {
        if (device == VK_NULL_HANDLE || fence == VK_NULL_HANDLE)
            return;

        vkDestroyFence(device, fence, nullptr);
        fence = VK_NULL_HANDLE;
    }

    void VulkanSynchronization::DestroySemaphore(VkDevice device, VkSemaphore& semaphore)
    {
        if (device == VK_NULL_HANDLE || semaphore == VK_NULL_HANDLE)
            return;

        vkDestroySemaphore(device, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }

} // namespace Engine
