#pragma once

#include <vulkan/vulkan.h>

namespace Engine
{

    // 同步对象轻量工厂。当前帧同步结构：
    //   - acquire：单个 host 等待的 fence（image 可用性由 CPU 确认，
    //     规避二值信号量与 swapchain image 的复用规则）
    //   - renderFinished：per swapchain image 信号量（present 过的图重新
    //     acquire 前，其关联信号量不得挪作他用）
    //   - in-flight：per frame-in-flight fence
    class VulkanSynchronization
    {
    public:
        static VkFence     CreateFence(VkDevice device, bool createSignaled);
        static VkSemaphore CreateSemaphore(VkDevice device);

        static void DestroyFence(VkDevice device, VkFence& fence);
        static void DestroySemaphore(VkDevice device, VkSemaphore& semaphore);
    };

} // namespace Engine
