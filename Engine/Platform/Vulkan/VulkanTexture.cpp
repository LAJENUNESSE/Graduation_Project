#include "engpch.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include <stb_image/stb_image.h>

#include "Asset/PathUtils.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"

namespace Engine
{

    // ============================================================================
    // VulkanImageUtils Implementation
    // ============================================================================

    VkCommandBuffer VulkanImageUtils::BeginSingleTimeCommands(VkDevice device, VkCommandPool commandPool)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void VulkanImageUtils::EndSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                                 VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void VulkanImageUtils::CreateImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width,
                                       uint32_t height, uint32_t mipLevels, uint32_t arrayLayers, VkFormat format,
                                       VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                                       VkImage& image, VkDeviceMemory& imageMemory, VkImageCreateFlags flags)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = mipLevels;
        imageInfo.arrayLayers   = arrayLayers;
        imageInfo.format        = format;
        imageInfo.tiling        = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage         = usage;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.flags         = flags;

        VkResult result = vkCreateImage(device, &imageInfo, nullptr, &image);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan image!");

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = VulkanBufferUtils::FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

        result = vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to allocate Vulkan image memory!");

        vkBindImageMemory(device, image, imageMemory, 0);
    }

    VkImageView VulkanImageUtils::CreateImageView(VkDevice device, VkImage image, VkFormat format,
                                                  VkImageAspectFlags aspectFlags, uint32_t mipLevels,
                                                  VkImageViewType viewType, uint32_t layerCount)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = image;
        viewInfo.viewType                        = viewType;
        viewInfo.format                          = format;
        viewInfo.subresourceRange.aspectMask     = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = layerCount;

        VkImageView imageView;
        VkResult    result = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan image view!");

        return imageView;
    }

    void VulkanImageUtils::TransitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                                 VkImage image, VkFormat format, VkImageLayout oldLayout,
                                                 VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount)
    {
        VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device, commandPool);

        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = oldLayout;
        barrier.newLayout                       = newLayout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = layerCount;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage           = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage      = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage           = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage           = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            ENGINE_CORE_ERROR("Unsupported layout transition: {} -> {}", static_cast<int>(oldLayout),
                              static_cast<int>(newLayout));
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = 0;
            sourceStage           = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage      = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        EndSingleTimeCommands(device, commandPool, graphicsQueue, commandBuffer);
    }

    void VulkanImageUtils::CopyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
                                             VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                                             uint32_t layerCount)
    {
        VkCommandBuffer commandBuffer = BeginSingleTimeCommands(device, commandPool);

        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = layerCount;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent                     = {width, height, 1};

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        EndSingleTimeCommands(device, commandPool, graphicsQueue, commandBuffer);
    }

    VkSampler VulkanImageUtils::CreateSampler(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t mipLevels,
                                              VkFilter magFilter, VkFilter minFilter, VkSamplerAddressMode addressMode)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter               = magFilter;
        samplerInfo.minFilter               = minFilter;
        samplerInfo.addressModeU            = addressMode;
        samplerInfo.addressModeV            = addressMode;
        samplerInfo.addressModeW            = addressMode;
        samplerInfo.anisotropyEnable        = VK_TRUE;
        samplerInfo.maxAnisotropy           = properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable           = VK_FALSE;
        samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias              = 0.0f;
        samplerInfo.minLod                  = 0.0f;
        samplerInfo.maxLod                  = static_cast<float>(mipLevels);

        VkSampler sampler;
        VkResult  result = vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan texture sampler!");

        return sampler;
    }

    // ============================================================================
    // VulkanTexture2D Implementation
    // ============================================================================

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height) : m_Width(width), m_Height(height)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        VkDevice         device         = context->GetDevice();
        VkPhysicalDevice physicalDevice = context->GetPhysicalDevice();

        // Create empty image
        VulkanImageUtils::CreateImage(device, physicalDevice, m_Width, m_Height, 1, 1, VK_FORMAT_R8G8B8A8_SRGB,
                                      VK_IMAGE_TILING_OPTIMAL,
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Image, m_ImageMemory);

        // Transition to shader read optimal (empty texture)
        VulkanImageUtils::TransitionImageLayout(device, context->GetCommandPool(), context->GetGraphicsQueue(), m_Image,
                                                VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

        m_ImageView = VulkanImageUtils::CreateImageView(device, m_Image, VK_FORMAT_R8G8B8A8_SRGB,
                                                        VK_IMAGE_ASPECT_COLOR_BIT, 1);
        m_Sampler   = VulkanImageUtils::CreateSampler(device, physicalDevice, 1);
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path) : m_Path(path)
    {
        int               width, height, channels;
        const std::string resolvedPath = PathUtils::ResolvePathString(path);
        stbi_set_flip_vertically_on_load(1);
        // Force RGBA output — handles 1/2/3/4 channel images uniformly
        stbi_uc* data = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);

        if (!data)
        {
            ENGINE_CORE_ERROR("Failed to load image: {0}", resolvedPath);
            CreateFallbackTexture();
            return;
        }

        m_Width  = width;
        m_Height = height;
        CreateTextureImage(data);
        stbi_image_free(data);
    }

    VulkanTexture2D::VulkanTexture2D(const void* data, uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height)
    {
        if (data)
        {
            CreateTextureImage(data);
        }
        else
        {
            CreateFallbackTexture();
        }
    }

    VulkanTexture2D::~VulkanTexture2D()
    {
        Cleanup();
    }

    void VulkanTexture2D::CreateTextureImage(const void* pixels)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        VkDevice         device         = context->GetDevice();
        VkPhysicalDevice physicalDevice = context->GetPhysicalDevice();
        VkCommandPool    commandPool    = context->GetCommandPool();
        VkQueue          graphicsQueue  = context->GetGraphicsQueue();

        VkDeviceSize imageSize = m_Width * m_Height * 4;

        // Create staging buffer
        VkBuffer       stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VulkanBufferUtils::CreateBuffer(device, physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        stagingBuffer, stagingBufferMemory);

        // Copy pixel data to staging buffer
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        // Create image
        VulkanImageUtils::CreateImage(device, physicalDevice, m_Width, m_Height, 1, 1, VK_FORMAT_R8G8B8A8_SRGB,
                                      VK_IMAGE_TILING_OPTIMAL,
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Image, m_ImageMemory);

        // Transition image layout for transfer
        VulkanImageUtils::TransitionImageLayout(device, commandPool, graphicsQueue, m_Image, VK_FORMAT_R8G8B8A8_SRGB,
                                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);

        // Copy buffer to image
        VulkanImageUtils::CopyBufferToImage(device, commandPool, graphicsQueue, stagingBuffer, m_Image, m_Width,
                                            m_Height);

        // Transition to shader read
        VulkanImageUtils::TransitionImageLayout(device, commandPool, graphicsQueue, m_Image, VK_FORMAT_R8G8B8A8_SRGB,
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

        // Cleanup staging buffer
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        // Create image view and sampler
        m_ImageView = VulkanImageUtils::CreateImageView(device, m_Image, VK_FORMAT_R8G8B8A8_SRGB,
                                                        VK_IMAGE_ASPECT_COLOR_BIT, 1);
        m_Sampler   = VulkanImageUtils::CreateSampler(device, physicalDevice, 1);
    }

    void VulkanTexture2D::CreateFallbackTexture()
    {
        m_Width  = 1;
        m_Height = 1;
        // Magenta fallback: RGBA = 0xFF, 0x00, 0xFF, 0xFF
        uint32_t magenta = 0xFFFF00FF;
        CreateTextureImage(&magenta);
    }

    void VulkanTexture2D::Cleanup()
    {
        auto* context = VulkanContext::Get();
        if (!context)
            return;

        VkDevice device = context->GetDevice();

        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        if (m_ImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, m_ImageView, nullptr);
            m_ImageView = VK_NULL_HANDLE;
        }
        if (m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
        }
        if (m_ImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, m_ImageMemory, nullptr);
            m_ImageMemory = VK_NULL_HANDLE;
        }
    }

    void VulkanTexture2D::SetData(void* data, uint32_t size)
    {
        uint32_t expected = m_Width * m_Height * 4;
        ENGINE_CORE_ASSERT(size == expected, "Data must be entire texture!");
        if (size != expected)
        {
            ENGINE_CORE_ERROR("Texture SetData size mismatch: expected {0}, got {1}", expected, size);
            return;
        }

        // Recreate texture with new data
        Cleanup();
        CreateTextureImage(data);
    }

    void VulkanTexture2D::Bind(uint32_t /*slot*/) const
    {
        // In Vulkan, binding is done through descriptor sets, not directly
        // This is a no-op for compatibility; actual binding happens via GetDescriptorInfo()
    }

    bool VulkanTexture2D::operator==(const Texture& other) const
    {
        return m_Image == static_cast<const VulkanTexture2D&>(other).m_Image;
    }

    VkDescriptorImageInfo VulkanTexture2D::GetDescriptorInfo() const
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView   = m_ImageView;
        imageInfo.sampler     = m_Sampler;
        return imageInfo;
    }

    // ============================================================================
    // VulkanTextureCubemap Implementation
    // ============================================================================

    VulkanTextureCubemap::VulkanTextureCubemap(const std::vector<std::string>& facePaths)
    {
        if (facePaths.size() != 6)
        {
            ENGINE_CORE_ERROR("Cubemap requires exactly 6 face paths, got {0}. Using fallback.", facePaths.size());
            CreateFallbackCubemap();
            return;
        }

        stbi_set_flip_vertically_on_load(0); // Cubemaps should NOT be flipped

        std::vector<void*> faceData(6, nullptr);
        std::vector<int>   faceWidths(6, 0);
        std::vector<int>   faceHeights(6, 0);
        bool               allLoaded = true;

        for (int i = 0; i < 6; i++)
        {
            int               width, height, channels;
            const std::string resolvedPath = PathUtils::ResolvePathString(facePaths[i]);
            faceData[i]                    = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);

            if (faceData[i])
            {
                faceWidths[i]  = width;
                faceHeights[i] = height;
                if (i == 0)
                {
                    m_Width  = width;
                    m_Height = height;
                }
            }
            else
            {
                ENGINE_CORE_ERROR("Cubemap face failed to load: {}", resolvedPath);
                allLoaded = false;
            }
        }

        stbi_set_flip_vertically_on_load(1); // Restore for Texture2D

        if (!allLoaded)
        {
            // Free any loaded data
            for (auto* ptr : faceData)
            {
                if (ptr)
                    stbi_image_free(ptr);
            }
            CreateFallbackCubemap();
            return;
        }

        CreateCubemapImage(faceData, faceWidths, faceHeights);

        // Free face data
        for (auto* ptr : faceData)
        {
            stbi_image_free(ptr);
        }
    }

    VulkanTextureCubemap::~VulkanTextureCubemap()
    {
        Cleanup();
    }

    void VulkanTextureCubemap::CreateCubemapImage(const std::vector<void*>& faceData, const std::vector<int>& faceWidths,
                                                  const std::vector<int>& faceHeights)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        VkDevice         device         = context->GetDevice();
        VkPhysicalDevice physicalDevice = context->GetPhysicalDevice();
        VkCommandPool    commandPool    = context->GetCommandPool();
        VkQueue          graphicsQueue  = context->GetGraphicsQueue();

        // Assume all faces are the same size
        VkDeviceSize layerSize  = m_Width * m_Height * 4;
        VkDeviceSize bufferSize = layerSize * 6;

        // Create staging buffer with all faces
        VkBuffer       stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VulkanBufferUtils::CreateBuffer(device, physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        stagingBuffer, stagingBufferMemory);

        // Copy all face data to staging buffer
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        for (int i = 0; i < 6; i++)
        {
            memcpy(static_cast<uint8_t*>(data) + i * layerSize, faceData[i], static_cast<size_t>(layerSize));
        }
        vkUnmapMemory(device, stagingBufferMemory);

        // Create cubemap image
        VulkanImageUtils::CreateImage(device, physicalDevice, m_Width, m_Height, 1, 6, VK_FORMAT_R8G8B8A8_SRGB,
                                      VK_IMAGE_TILING_OPTIMAL,
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Image, m_ImageMemory,
                                      VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

        // Transition for transfer
        VulkanImageUtils::TransitionImageLayout(device, commandPool, graphicsQueue, m_Image, VK_FORMAT_R8G8B8A8_SRGB,
                                                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 6);

        // Copy buffer to cubemap faces
        VkCommandBuffer cmdBuffer = VulkanImageUtils::BeginSingleTimeCommands(device, commandPool);

        std::vector<VkBufferImageCopy> regions(6);
        for (int i = 0; i < 6; i++)
        {
            regions[i].bufferOffset                    = i * layerSize;
            regions[i].bufferRowLength                 = 0;
            regions[i].bufferImageHeight               = 0;
            regions[i].imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[i].imageSubresource.mipLevel       = 0;
            regions[i].imageSubresource.baseArrayLayer = i;
            regions[i].imageSubresource.layerCount     = 1;
            regions[i].imageOffset                     = {0, 0, 0};
            regions[i].imageExtent                     = {m_Width, m_Height, 1};
        }

        vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());

        VulkanImageUtils::EndSingleTimeCommands(device, commandPool, graphicsQueue, cmdBuffer);

        // Transition to shader read
        VulkanImageUtils::TransitionImageLayout(device, commandPool, graphicsQueue, m_Image, VK_FORMAT_R8G8B8A8_SRGB,
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 6);

        // Cleanup staging buffer
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        // Create cubemap image view
        m_ImageView = VulkanImageUtils::CreateImageView(device, m_Image, VK_FORMAT_R8G8B8A8_SRGB,
                                                        VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_CUBE, 6);

        // Create sampler with clamp-to-edge for cubemaps
        m_Sampler = VulkanImageUtils::CreateSampler(device, physicalDevice, 1, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    }

    void VulkanTextureCubemap::CreateFallbackCubemap()
    {
        m_Width  = 1;
        m_Height = 1;

        // Create 6 magenta fallback faces
        std::vector<void*> faceData(6);
        std::vector<int>   faceWidths(6, 1);
        std::vector<int>   faceHeights(6, 1);
        uint32_t           magenta = 0xFFFF00FF;

        for (int i = 0; i < 6; i++)
        {
            faceData[i] = malloc(4);
            memcpy(faceData[i], &magenta, 4);
        }

        CreateCubemapImage(faceData, faceWidths, faceHeights);

        for (auto* ptr : faceData)
        {
            free(ptr);
        }
    }

    void VulkanTextureCubemap::Cleanup()
    {
        auto* context = VulkanContext::Get();
        if (!context)
            return;

        VkDevice device = context->GetDevice();

        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, m_Sampler, nullptr);
            m_Sampler = VK_NULL_HANDLE;
        }
        if (m_ImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, m_ImageView, nullptr);
            m_ImageView = VK_NULL_HANDLE;
        }
        if (m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
        }
        if (m_ImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, m_ImageMemory, nullptr);
            m_ImageMemory = VK_NULL_HANDLE;
        }
    }

    void VulkanTextureCubemap::SetData(void* /*data*/, uint32_t /*size*/)
    {
        ENGINE_CORE_WARN("VulkanTextureCubemap::SetData not implemented");
    }

    void VulkanTextureCubemap::Bind(uint32_t /*slot*/) const
    {
        // In Vulkan, binding is done through descriptor sets, not directly
    }

    bool VulkanTextureCubemap::operator==(const Texture& other) const
    {
        return m_Image == static_cast<const VulkanTextureCubemap&>(other).m_Image;
    }

    VkDescriptorImageInfo VulkanTextureCubemap::GetDescriptorInfo() const
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView   = m_ImageView;
        imageInfo.sampler     = m_Sampler;
        return imageInfo;
    }

} // namespace Engine
