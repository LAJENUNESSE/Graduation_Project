#include "engpch.h"
#include "Platform/Vulkan/VulkanAllocator.h"

#include "Core/Assert.h"

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

namespace Engine
{
    VmaAllocator VulkanAllocator::s_Allocator = nullptr;

    void VulkanAllocator::Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
    {
        if (s_Allocator != nullptr)
            return;

        VmaAllocatorCreateInfo createInfo{};
        createInfo.instance         = instance;
        createInfo.physicalDevice   = physicalDevice;
        createInfo.device           = device;
        createInfo.vulkanApiVersion = VK_API_VERSION_1_2;

        const VkResult result = vmaCreateAllocator(&createInfo, &s_Allocator);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create VMA allocator!");
    }

    void VulkanAllocator::Shutdown()
    {
        if (s_Allocator == nullptr)
            return;

        vmaDestroyAllocator(s_Allocator);
        s_Allocator = nullptr;
    }

    bool VulkanAllocator::IsInitialized()
    {
        return s_Allocator != nullptr;
    }

    VmaAllocator VulkanAllocator::GetAllocator()
    {
        ENGINE_CORE_RELEASE_ASSERT(s_Allocator != nullptr, "VMA allocator is not initialized!");
        return s_Allocator;
    }

} // namespace Engine
