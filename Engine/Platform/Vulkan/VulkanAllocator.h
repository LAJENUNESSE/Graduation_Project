#pragma once

#include <vulkan/vulkan.h>

struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace Engine
{
    class VulkanAllocator
    {
    public:
        static void Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
        static void Shutdown();
        static bool IsInitialized();

        static VmaAllocator GetAllocator();

    private:
        static VmaAllocator s_Allocator;
    };

} // namespace Engine
