#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/Texture.h"

namespace Engine
{

    // ============================================================================
    // VulkanImageUtils - Static helper functions for Vulkan image operations
    // ============================================================================
    class VulkanImageUtils
    {
    public:
        static void CreateImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                                uint32_t mipLevels, uint32_t arrayLayers, VkFormat format, VkImageTiling tiling,
                                VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                                VkDeviceMemory& imageMemory, VkImageCreateFlags flags = 0);

        static VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format,
                                           VkImageAspectFlags aspectFlags, uint32_t mipLevels,
                                           VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D, uint32_t layerCount = 1);

        static void TransitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                          VkImage image, VkFormat format, VkImageLayout oldLayout,
                                          VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount = 1);

        static void CopyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                      VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                                      uint32_t layerCount = 1);

        static VkSampler CreateSampler(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t mipLevels,
                                       VkFilter magFilter = VK_FILTER_LINEAR, VkFilter minFilter = VK_FILTER_LINEAR,
                                       VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

        // Single-time command buffer helpers (public for cubemap and other complex operations)
        static VkCommandBuffer BeginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
        static void            EndSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                                     VkCommandBuffer commandBuffer);
    };

    // ============================================================================
    // VulkanTexture2D - 2D texture implementation for Vulkan
    // ============================================================================
    class VulkanTexture2D : public Texture2D
    {
    public:
        VulkanTexture2D(uint32_t width, uint32_t height);
        VulkanTexture2D(const std::string& path);
        VulkanTexture2D(const void* data, uint32_t width, uint32_t height);
        ~VulkanTexture2D() override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return 0; } // Not applicable for Vulkan

        void SetData(void* data, uint32_t size) override;
        void Bind(uint32_t slot = 0) const override;

        bool operator==(const Texture& other) const override;

        // Vulkan-specific accessors
        VkImage        GetImage() const { return m_Image; }
        VkImageView    GetImageView() const { return m_ImageView; }
        VkSampler      GetSampler() const { return m_Sampler; }
        VkDescriptorImageInfo GetDescriptorInfo() const;

    private:
        void CreateTextureImage(const void* pixels);
        void CreateFallbackTexture();
        void Cleanup();

        std::string    m_Path;
        uint32_t       m_Width      = 0;
        uint32_t       m_Height     = 0;
        VkImage        m_Image      = VK_NULL_HANDLE;
        VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE;
        VkImageView    m_ImageView  = VK_NULL_HANDLE;
        VkSampler      m_Sampler    = VK_NULL_HANDLE;
    };

    // ============================================================================
    // VulkanTextureCubemap - Cubemap texture implementation for Vulkan
    // ============================================================================
    class VulkanTextureCubemap : public TextureCubemap
    {
    public:
        // faces order: +X, -X, +Y, -Y, +Z, -Z
        VulkanTextureCubemap(const std::vector<std::string>& facePaths);
        ~VulkanTextureCubemap() override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return 0; } // Not applicable for Vulkan

        void SetData(void* data, uint32_t size) override;
        void Bind(uint32_t slot = 0) const override;

        bool operator==(const Texture& other) const override;

        // Vulkan-specific accessors
        VkImage        GetImage() const { return m_Image; }
        VkImageView    GetImageView() const { return m_ImageView; }
        VkSampler      GetSampler() const { return m_Sampler; }
        VkDescriptorImageInfo GetDescriptorInfo() const;

    private:
        void CreateCubemapImage(const std::vector<void*>& faceData, const std::vector<int>& faceWidths,
                                const std::vector<int>& faceHeights);
        void CreateFallbackCubemap();
        void Cleanup();

        uint32_t       m_Width       = 0;
        uint32_t       m_Height      = 0;
        VkImage        m_Image       = VK_NULL_HANDLE;
        VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE;
        VkImageView    m_ImageView   = VK_NULL_HANDLE;
        VkSampler      m_Sampler     = VK_NULL_HANDLE;
    };

} // namespace Engine
