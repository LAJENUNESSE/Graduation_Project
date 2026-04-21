#pragma once

#include "Renderer/Texture.h"

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace Engine
{

    class VulkanTexture2D : public Texture2D
    {
    public:
        VulkanTexture2D(uint32_t width, uint32_t height);
        VulkanTexture2D(const std::string& path);
        VulkanTexture2D(const void* data, uint32_t width, uint32_t height);
        VulkanTexture2D(uint32_t width, uint32_t height, const TextureSpecification& spec);
        VulkanTexture2D(const void* data, uint32_t width, uint32_t height, const TextureSpecification& spec);
        ~VulkanTexture2D() override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return 0; }

        void SetData(void* data, uint32_t size) override;
        void Bind(uint32_t slot = 0) const override;

        bool operator==(const Texture& other) const override;

        VkImageView GetImageView() const { return m_ImageView; }
        VkSampler   GetSampler() const { return m_Sampler; }

    private:
        void CreateImage(uint32_t width, uint32_t height, VkFormat format);
        void CreateImageView(VkFormat format);
        void CreateSampler(VkFilter minFilter, VkFilter magFilter, VkSamplerAddressMode addressMode);
        void UploadPixels(const void* data, uint32_t dataSize);
        void TransitionLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
        void Destroy();

    private:
        uint32_t      m_Width      = 0;
        uint32_t      m_Height     = 0;
        VkImage       m_Image      = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = nullptr;
        VkImageView   m_ImageView  = VK_NULL_HANDLE;
        VkSampler     m_Sampler    = VK_NULL_HANDLE;
        VkFormat      m_Format     = VK_FORMAT_R8G8B8A8_UNORM;
    };

    class VulkanTextureCubemap : public TextureCubemap
    {
    public:
        VulkanTextureCubemap(const std::vector<std::string>& facePaths);
        ~VulkanTextureCubemap() override = default;

        uint32_t GetWidth() const override { return 0; }
        uint32_t GetHeight() const override { return 0; }
        uint32_t GetRendererID() const override { return 0; }

        void SetData(void* data, uint32_t size) override;
        void Bind(uint32_t slot = 0) const override;

        bool operator==(const Texture& other) const override { return false; }
    };

} // namespace Engine
