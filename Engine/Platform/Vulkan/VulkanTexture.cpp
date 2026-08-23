#include "engpch.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include "Asset/PathUtils.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/Vulkan/VulkanAllocator.h"
#include "Platform/Vulkan/VulkanContext.h"

#include <cstring>
#include <stb_image/stb_image.h>
#include <vma/vk_mem_alloc.h>

namespace Engine
{
    namespace
    {
        VkFormat TextureFormatToVk(TextureFormat fmt)
        {
            switch (fmt)
            {
            case TextureFormat::RGBA8:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RGBA16F:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case TextureFormat::RG16F:
                return VK_FORMAT_R16G16_SFLOAT;
            case TextureFormat::R32F:
                return VK_FORMAT_R32_SFLOAT;
            case TextureFormat::R16F:
                return VK_FORMAT_R16_SFLOAT;
            }
            return VK_FORMAT_R8G8B8A8_UNORM;
        }

        VkFilter TextureFilterToVk(TextureFilter f)
        {
            return f == TextureFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        }

        VkSamplerAddressMode TextureWrapToVk(TextureWrap w)
        {
            return w == TextureWrap::ClampToEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                 : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    } // namespace

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height)
    {
        m_Format = VK_FORMAT_R8G8B8A8_UNORM;
        CreateImage(width, height, m_Format);
        CreateImageView(m_Format);
        CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        TransitionLayout(m_Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path)
    {
        int               width, height, channels;
        const std::string resolvedPath = PathUtils::ResolvePathString(path);
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* data = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);

        m_Format = VK_FORMAT_R8G8B8A8_UNORM;

        if (!data)
        {
            ENGINE_CORE_ERROR("Failed to load image: {0}", resolvedPath);
            CreateImage(1, 1, m_Format);
            CreateImageView(m_Format);
            CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

            uint32_t magenta = 0xFFFF00FF;
            UploadPixels(&magenta, sizeof(magenta));
            return;
        }

        CreateImage(static_cast<uint32_t>(width), static_cast<uint32_t>(height), m_Format);
        CreateImageView(m_Format);
        CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

        const uint32_t dataSize = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4;
        UploadPixels(data, dataSize);
        stbi_image_free(data);
    }

    VulkanTexture2D::VulkanTexture2D(const void* data, uint32_t width, uint32_t height)
    {
        m_Format = VK_FORMAT_R8G8B8A8_UNORM;
        CreateImage(width, height, m_Format);
        CreateImageView(m_Format);
        CreateSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

        if (data)
            UploadPixels(data, width * height * 4);
        else
            TransitionLayout(m_Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height, const TextureSpecification& spec)
    {
        m_Format = TextureFormatToVk(spec.Format);
        CreateImage(width, height, m_Format);
        CreateImageView(m_Format);
        CreateSampler(TextureFilterToVk(spec.MinFilter), TextureFilterToVk(spec.MagFilter),
                      TextureWrapToVk(spec.WrapS));
        TransitionLayout(m_Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VulkanTexture2D::VulkanTexture2D(const void*                 data,
                                     uint32_t                    width,
                                     uint32_t                    height,
                                     const TextureSpecification& spec)
    {
        m_Format = TextureFormatToVk(spec.Format);
        CreateImage(width, height, m_Format);
        CreateImageView(m_Format);
        CreateSampler(TextureFilterToVk(spec.MinFilter), TextureFilterToVk(spec.MagFilter),
                      TextureWrapToVk(spec.WrapS));

        if (data)
            UploadPixels(data, width * height * 4);
        else
            TransitionLayout(m_Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VulkanTexture2D::~VulkanTexture2D()
    {
        Destroy();
    }

    void VulkanTexture2D::SetData(void* data, uint32_t size)
    {
        const uint32_t expected = m_Width * m_Height * 4;
        ENGINE_CORE_ASSERT(size == expected, "Data must be entire texture!");
        if (size != expected)
        {
            ENGINE_CORE_ERROR("Texture SetData size mismatch: expected {0}, got {1}", expected, size);
            return;
        }
        GpuMemoryStats::Get().AddUploaded(size);
        UploadPixels(data, size);
    }

    // Phase 8.2：注册到场景状态机纹理槽（slot 即 OpenGL unit 号语义）
    void VulkanTexture2D::Bind(uint32_t slot) const
    {
        if (auto* context = VulkanContext::Get())
            context->GetSceneState().BindTextureSlot(slot, m_ImageView, m_Sampler);
    }

    bool VulkanTexture2D::operator==(const Texture& other) const
    {
        return m_Image == static_cast<const VulkanTexture2D&>(other).m_Image;
    }

    void VulkanTexture2D::CreateImage(uint32_t width, uint32_t height, VkFormat format)
    {
        m_Width  = width;
        m_Height = height;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.format        = format;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        const VkResult result =
            vmaCreateImage(VulkanAllocator::GetAllocator(), &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan image via VMA");
    }

    void VulkanTexture2D::CreateImageView(VkFormat format)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = m_Image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = format;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        const VkResult result = vkCreateImageView(context->GetDevice(), &viewInfo, nullptr, &m_ImageView);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan image view");
    }

    void VulkanTexture2D::CreateSampler(VkFilter minFilter, VkFilter magFilter, VkSamplerAddressMode addressMode)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter               = magFilter;
        samplerInfo.minFilter               = minFilter;
        samplerInfo.addressModeU            = addressMode;
        samplerInfo.addressModeV            = addressMode;
        samplerInfo.addressModeW            = addressMode;
        samplerInfo.anisotropyEnable        = VK_FALSE;
        samplerInfo.maxAnisotropy           = 1.0f;
        samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable           = VK_FALSE;
        samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        const VkResult result = vkCreateSampler(context->GetDevice(), &samplerInfo, nullptr, &m_Sampler);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan sampler");
    }

    void VulkanTexture2D::UploadPixels(const void* data, uint32_t dataSize)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");
        ENGINE_CORE_RELEASE_ASSERT(data != nullptr, "Pixel data must not be null");

        VkBuffer      stagingBuffer     = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = nullptr;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = dataSize;
        bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkResult result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer,
                                          &stagingAllocation, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer for texture upload");

        void* mappedData = nullptr;
        result           = vmaMapMemory(VulkanAllocator::GetAllocator(), stagingAllocation, &mappedData);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map staging buffer");
        std::memcpy(mappedData, data, dataSize);
        vmaUnmapMemory(VulkanAllocator::GetAllocator(), stagingAllocation);

        TransitionLayout(m_Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset                    = 0;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = {0, 0, 0};
        region.imageExtent                     = {m_Width, m_Height, 1};

        vkCmdCopyBufferToImage(cmd, stagingBuffer, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        context->EndSingleTimeCommands(cmd);

        TransitionLayout(m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vmaDestroyBuffer(VulkanAllocator::GetAllocator(), stagingBuffer, stagingAllocation);
    }

    void VulkanTexture2D::TransitionLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");

        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = oldLayout;
        barrier.newLayout                       = newLayout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage              = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        context->EndSingleTimeCommands(cmd);
    }

    void VulkanTexture2D::Destroy()
    {
        auto* context = VulkanContext::Get();
        if (!context)
            return;

        VkDevice device = context->GetDevice();

        if (m_Sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, m_Sampler, nullptr);

        if (m_ImageView != VK_NULL_HANDLE)
            vkDestroyImageView(device, m_ImageView, nullptr);

        if (m_Image != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyImage(VulkanAllocator::GetAllocator(), m_Image, m_Allocation);
    }

    VulkanTextureCubemap::VulkanTextureCubemap(const std::vector<std::string>& facePaths)
    {
        m_Format = VK_FORMAT_R8G8B8A8_UNORM;

        auto buildMagentaFallback = [&]()
        {
            m_FaceSize = 1;
            CreateImage(m_FaceSize, m_Format);
            CreateCubeView(m_Format);
            CreateSampler();

            constexpr uint32_t kMagenta = 0xFFFF00FF;
            uint32_t           pixels[6];
            for (int i = 0; i < 6; ++i)
                pixels[i] = kMagenta;
            UploadFaces(pixels, m_FaceSize);
        };

        if (facePaths.size() != 6)
        {
            ENGINE_CORE_ERROR("Cubemap requires exactly 6 face paths, got {0}. Using 1x1 magenta fallback.",
                              facePaths.size());
            buildMagentaFallback();
            return;
        }

        // 加载 6 面到内存；任一面失败或尺寸不一致 → 整体降级
        stbi_set_flip_vertically_on_load(0); // cubemap 不翻转，对齐 OpenGL
        struct FaceData
        {
            stbi_uc* pixels = nullptr;
            int      w      = 0;
            int      h      = 0;
        };
        FaceData faces[6];
        bool     ok   = true;
        int      refW = 0, refH = 0;
        for (int i = 0; i < 6; ++i)
        {
            int               channels     = 0;
            const std::string resolvedPath = PathUtils::ResolvePathString(facePaths[i]);
            faces[i].pixels                = stbi_load(resolvedPath.c_str(), &faces[i].w, &faces[i].h, &channels, 4);
            if (!faces[i].pixels)
            {
                ENGINE_CORE_ERROR("Cubemap face failed to load: {}", resolvedPath);
                ok = false;
                break;
            }
            if (i == 0)
            {
                refW = faces[i].w;
                refH = faces[i].h;
            }
            else if (faces[i].w != refW || faces[i].h != refH)
            {
                ENGINE_CORE_ERROR("Cubemap face size mismatch: face {} is {}x{}, expected {}x{}", i, faces[i].w,
                                  faces[i].h, refW, refH);
                ok = false;
                break;
            }
        }
        stbi_set_flip_vertically_on_load(1); // restore for Texture2D

        if (!ok || refW <= 0 || refW != refH)
        {
            for (auto& f : faces)
                if (f.pixels)
                    stbi_image_free(f.pixels);
            buildMagentaFallback();
            return;
        }

        m_FaceSize = static_cast<uint32_t>(refW);

        CreateImage(m_FaceSize, m_Format);
        CreateCubeView(m_Format);
        CreateSampler();

        // 6 面合并到连续 buffer 中，UploadFaces 单 staging + 单次 vkCmdCopyBufferToImage 6 region
        const size_t         faceBytes = static_cast<size_t>(m_FaceSize) * m_FaceSize * 4;
        std::vector<stbi_uc> contiguous(faceBytes * 6);
        for (int i = 0; i < 6; ++i)
        {
            std::memcpy(contiguous.data() + i * faceBytes, faces[i].pixels, faceBytes);
            stbi_image_free(faces[i].pixels);
        }

        UploadFaces(contiguous.data(), m_FaceSize);
    }

    VulkanTextureCubemap::~VulkanTextureCubemap()
    {
        Destroy();
    }

    void VulkanTextureCubemap::SetData(void* /*data*/, uint32_t /*size*/)
    {
        ENGINE_CORE_WARN("[Vulkan] TextureCubemap::SetData not implemented");
    }

    // Phase 8.2：注册到场景状态机纹理槽（slot 即 OpenGL unit 号语义）
    void VulkanTextureCubemap::Bind(uint32_t slot) const
    {
        if (auto* context = VulkanContext::Get())
            context->GetSceneState().BindTextureSlot(slot, m_ImageView, m_Sampler);
    }

    bool VulkanTextureCubemap::operator==(const Texture& other) const
    {
        return m_Image == static_cast<const VulkanTextureCubemap&>(other).m_Image;
    }

    void VulkanTextureCubemap::CreateImage(uint32_t faceSize, VkFormat format)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width  = faceSize;
        imageInfo.extent.height = faceSize;
        imageInfo.extent.depth  = 1;
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 6;
        imageInfo.format        = format;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        const VkResult result =
            vmaCreateImage(VulkanAllocator::GetAllocator(), &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan cubemap image via VMA");
    }

    void VulkanTextureCubemap::CreateCubeView(VkFormat format)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = m_Image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format                          = format;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 6;

        const VkResult result = vkCreateImageView(context->GetDevice(), &viewInfo, nullptr, &m_ImageView);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan cubemap image view");
    }

    void VulkanTextureCubemap::CreateSampler()
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter               = VK_FILTER_LINEAR;
        samplerInfo.minFilter               = VK_FILTER_LINEAR;
        samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable        = VK_FALSE;
        samplerInfo.maxAnisotropy           = 1.0f;
        samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable           = VK_FALSE;
        samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        const VkResult result = vkCreateSampler(context->GetDevice(), &samplerInfo, nullptr, &m_Sampler);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan cubemap sampler");
    }

    void VulkanTextureCubemap::UploadFaces(const void* faceContiguousRGBA8, uint32_t faceSize)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");
        ENGINE_CORE_RELEASE_ASSERT(faceContiguousRGBA8 != nullptr, "Cubemap pixel data must not be null");

        const uint32_t faceBytes  = faceSize * faceSize * 4;
        const uint32_t totalBytes = faceBytes * 6;

        VkBuffer      stagingBuffer     = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = nullptr;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = totalBytes;
        bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkResult result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer,
                                          &stagingAllocation, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer for cubemap upload");

        void* mappedData = nullptr;
        result           = vmaMapMemory(VulkanAllocator::GetAllocator(), stagingAllocation, &mappedData);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map cubemap staging buffer");
        std::memcpy(mappedData, faceContiguousRGBA8, totalBytes);
        vmaUnmapMemory(VulkanAllocator::GetAllocator(), stagingAllocation);

        TransitionLayoutCube(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkBufferImageCopy regions[6]{};
        for (int i = 0; i < 6; ++i)
        {
            regions[i].bufferOffset                    = static_cast<VkDeviceSize>(i) * faceBytes;
            regions[i].bufferRowLength                 = 0;
            regions[i].bufferImageHeight               = 0;
            regions[i].imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[i].imageSubresource.mipLevel       = 0;
            regions[i].imageSubresource.baseArrayLayer = static_cast<uint32_t>(i);
            regions[i].imageSubresource.layerCount     = 1;
            regions[i].imageOffset                     = {0, 0, 0};
            regions[i].imageExtent                     = {faceSize, faceSize, 1};
        }

        vkCmdCopyBufferToImage(cmd, stagingBuffer, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions);

        context->EndSingleTimeCommands(cmd);

        TransitionLayoutCube(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vmaDestroyBuffer(VulkanAllocator::GetAllocator(), stagingBuffer, stagingAllocation);
    }

    void VulkanTextureCubemap::TransitionLayoutCube(VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required");

        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = oldLayout;
        barrier.newLayout                       = newLayout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = m_Image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 6;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage              = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        context->EndSingleTimeCommands(cmd);
    }

    void VulkanTextureCubemap::Destroy()
    {
        auto* context = VulkanContext::Get();
        if (!context)
            return;

        VkDevice device = context->GetDevice();

        if (m_Sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, m_Sampler, nullptr);

        if (m_ImageView != VK_NULL_HANDLE)
            vkDestroyImageView(device, m_ImageView, nullptr);

        if (m_Image != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyImage(VulkanAllocator::GetAllocator(), m_Image, m_Allocation);
    }

} // namespace Engine
