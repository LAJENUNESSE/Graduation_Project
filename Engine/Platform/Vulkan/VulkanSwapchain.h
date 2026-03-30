#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Engine
{

    class VulkanSwapchain
    {
    public:
        VulkanSwapchain(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface, uint32_t width,
                        uint32_t height);
        ~VulkanSwapchain();

        void Create(uint32_t width, uint32_t height);
        void Cleanup();

        VkSwapchainKHR      GetSwapchain() const { return m_Swapchain; }
        VkFormat            GetImageFormat() const { return m_ImageFormat; }
        VkExtent2D          GetExtent() const { return m_Extent; }
        const std::vector<VkImage>&     GetImages() const { return m_Images; }
        const std::vector<VkImageView>& GetImageViews() const { return m_ImageViews; }

        uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Images.size()); }

    private:
        struct SwapchainSupportDetails
        {
            VkSurfaceCapabilitiesKHR        capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR>   presentModes;
        };

        SwapchainSupportDetails QuerySwapchainSupport();
        VkSurfaceFormatKHR      ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR        ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D              ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width,
                                                 uint32_t height);

    private:
        VkPhysicalDevice m_PhysicalDevice;
        VkDevice         m_Device;
        VkSurfaceKHR     m_Surface;

        VkSwapchainKHR         m_Swapchain   = VK_NULL_HANDLE;
        VkFormat               m_ImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D             m_Extent      = {0, 0};
        std::vector<VkImage>     m_Images;
        std::vector<VkImageView> m_ImageViews;
    };

} // namespace Engine
