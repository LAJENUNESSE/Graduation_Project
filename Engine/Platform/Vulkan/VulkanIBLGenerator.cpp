#include "engpch.h"
#include "Platform/Vulkan/VulkanIBLGenerator.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanAllocator.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDescriptor.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Renderer/Shader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <vma/vk_mem_alloc.h>

namespace Engine
{

    namespace
    {
        // ---- helpers ----
        VkDevice GetDevice()
        {
            auto* ctx = VulkanContext::Get();
            ENGINE_CORE_RELEASE_ASSERT(ctx != nullptr, "[VulkanIBL] VulkanContext is null");
            return ctx->GetDevice();
        }

        // 创建一个简单的 linear-clamp sampler，给 IBL 输出贴图用（供 PBR pass 采样）
        VkSampler CreateLinearClampSampler(VkDevice device)
        {
            VkSamplerCreateInfo info{};
            info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            info.magFilter    = VK_FILTER_LINEAR;
            info.minFilter    = VK_FILTER_LINEAR;
            info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.borderColor  = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

            VkSampler s = VK_NULL_HANDLE;
            VkResult  r = vkCreateSampler(device, &info, nullptr, &s);
            ENGINE_CORE_RELEASE_ASSERT(r == VK_SUCCESS, "[VulkanIBL] Failed to create sampler");
            return s;
        }
    } // namespace

    bool VulkanIBLGenerator::Init()
    {
        auto* ctx    = VulkanContext::Get();
        auto  device = GetDevice();

        // 1) 加载 3 个 compute shader（共享 .glsl，VulkanShader 自动注入 VULKAN macro 走 push_constant 路径）
        auto loadShader = [](const std::string& path) -> Ref<VulkanShader>
        {
            auto base = Shader::Create(path);
            // 在 Vulkan 后端下 Shader::Create 返回 VulkanShader；编译失败会在内部 RELEASE_ASSERT
            return std::static_pointer_cast<VulkanShader>(base);
        };

        m_BRDFLutShader    = loadShader("assets/shaders/IBL_BRDF_LUT.glsl");
        m_IrradianceShader = loadShader("assets/shaders/IBL_Irradiance.glsl");
        m_PrefilterShader  = loadShader("assets/shaders/IBL_Prefilter.glsl");

        if (!m_BRDFLutShader || !m_IrradianceShader || !m_PrefilterShader)
        {
            ENGINE_CORE_ERROR("[VulkanIBL] One or more IBL shaders failed to load");
            return false;
        }

        // 2) 从反射结果建 descriptor set layouts
        m_BRDFLutSetLayout =
            VulkanDescriptorSetLayout::CreateFromReflection(device, m_BRDFLutShader->GetReflectedBindings(), 0);
        m_IrradianceSetLayout =
            VulkanDescriptorSetLayout::CreateFromReflection(device, m_IrradianceShader->GetReflectedBindings(), 0);
        m_PrefilterSetLayout =
            VulkanDescriptorSetLayout::CreateFromReflection(device, m_PrefilterShader->GetReflectedBindings(), 0);

        // 3) descriptor pool（IBL 预计算总共最多 ~7 个 set：BRDF + Irradiance + 5 mip prefilter）
        m_DescriptorPool = VulkanDescriptorPool::CreateDefaultComputePool(device, 16);

        // 4) 构造 3 条 compute pipeline
        auto buildPipeline = [&](const Ref<VulkanShader>&              shader,
                                 const Ref<VulkanDescriptorSetLayout>& setLayout) -> PipelineHandles
        {
            VulkanComputePipelineDesc desc{};
            desc.ShaderModule = shader->GetOrCreateShaderModule(device, "compute");
            desc.EntryPoint   = "main";
            desc.SetLayouts   = {setLayout->GetHandle()};

            // push constant range：取反射结果（每个 IBL shader 都有一个，stage = compute）
            for (const auto& pc : shader->GetReflectedPushConstants())
            {
                VkPushConstantRange r{};
                r.offset     = pc.Offset;
                r.size       = pc.Size;
                r.stageFlags = pc.Stages;
                desc.PushConstants.push_back(r);
            }

            auto handle = VulkanPipeline::CreateCompute(device, desc);
            return {handle.Pipeline, handle.Layout};
        };

        m_BRDFLutPipeline    = buildPipeline(m_BRDFLutShader, m_BRDFLutSetLayout);
        m_IrradiancePipeline = buildPipeline(m_IrradianceShader, m_IrradianceSetLayout);
        m_PrefilterPipeline  = buildPipeline(m_PrefilterShader, m_PrefilterSetLayout);

        // 5) 公用 sampler
        m_LinearSampler = CreateLinearClampSampler(device);

        ENGINE_CORE_INFO("[VulkanIBL] All compute shaders + pipelines created successfully");

        // 6) 立即生成 BRDF LUT（与 skybox 无关，可在 Init 阶段一次性算好）
        GenerateBRDFLut();

        (void)ctx;
        return true;
    }

    void VulkanIBLGenerator::Shutdown()
    {
        Clear();

        auto device = GetDevice();
        vkDeviceWaitIdle(device);

        DestroyImage(m_BRDFLut);
        m_BRDFLutView = VK_NULL_HANDLE;

        VulkanComputePipelineHandle h;
        h.Pipeline = m_BRDFLutPipeline.Pipeline;
        h.Layout   = m_BRDFLutPipeline.Layout;
        VulkanPipeline::DestroyCompute(device, h);
        m_BRDFLutPipeline = {};

        h.Pipeline = m_IrradiancePipeline.Pipeline;
        h.Layout   = m_IrradiancePipeline.Layout;
        VulkanPipeline::DestroyCompute(device, h);
        m_IrradiancePipeline = {};

        h.Pipeline = m_PrefilterPipeline.Pipeline;
        h.Layout   = m_PrefilterPipeline.Layout;
        VulkanPipeline::DestroyCompute(device, h);
        m_PrefilterPipeline = {};

        m_BRDFLutSetLayout.reset();
        m_IrradianceSetLayout.reset();
        m_PrefilterSetLayout.reset();
        m_DescriptorPool.reset();

        if (m_LinearSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, m_LinearSampler, nullptr);
            m_LinearSampler = VK_NULL_HANDLE;
        }

        m_BRDFLutShader.reset();
        m_IrradianceShader.reset();
        m_PrefilterShader.reset();
    }

    void VulkanIBLGenerator::Clear()
    {
        // Irradiance/Prefilter 在不同 skybox 间被重建，BRDF LUT 不动
        auto device = GetDevice();
        vkDeviceWaitIdle(device);

        DestroyImage(m_Irradiance);
        DestroyImage(m_Prefilter);
        m_IrradianceView = VK_NULL_HANDLE;
        m_PrefilterView  = VK_NULL_HANDLE;
        m_IBLReady       = false;
    }

    // ---- 工具：创建可被 storage_image 写入的 2D 纹理 ----
    VulkanIBLGenerator::ImageHandle
    VulkanIBLGenerator::CreateStorageImage2D(uint32_t width, uint32_t height, VkFormat format)
    {
        ImageHandle out{};
        auto        device = GetDevice();

        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.extent.width  = width;
        imgInfo.extent.height = height;
        imgInfo.extent.depth  = 1;
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.format        = format;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // STORAGE 供 compute 写，SAMPLED 供 PBR 采样，TRANSFER_DST 留出未来上传初值的路径
        imgInfo.usage       = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.samples     = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VkResult r =
            vmaCreateImage(VulkanAllocator::GetAllocator(), &imgInfo, &allocInfo, &out.Image, &out.Allocation, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(r == VK_SUCCESS, "[VulkanIBL] vmaCreateImage failed");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = out.Image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = format;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        r = vkCreateImageView(device, &viewInfo, nullptr, &out.View);
        ENGINE_CORE_RELEASE_ASSERT(r == VK_SUCCESS, "[VulkanIBL] vkCreateImageView failed");

        return out;
    }

    void VulkanIBLGenerator::DestroyImage(ImageHandle& img)
    {
        const VkImageView   viewToDestroy       = img.View;
        const VkImage       imageToDestroy      = img.Image;
        const VmaAllocation allocationToDestroy = img.Allocation;

        img = {};

        if (viewToDestroy == VK_NULL_HANDLE && imageToDestroy == VK_NULL_HANDLE)
            return;

        // 切换天空盒时旧 IBL 仍可能被当前主帧命令缓冲引用。即使
        // vkDeviceWaitIdle 已等待所有已提交工作，也保护不了正在录制的命令缓冲，
        // 因此与 Framebuffer/Texture/Buffer 一致延迟到对应帧 fence 完成后释放。
        VulkanContext::DeferDestroy(
            [viewToDestroy, imageToDestroy, allocationToDestroy](VkDevice device)
            {
                if (viewToDestroy != VK_NULL_HANDLE)
                    vkDestroyImageView(device, viewToDestroy, nullptr);
                if (imageToDestroy != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
                    vmaDestroyImage(VulkanAllocator::GetAllocator(), imageToDestroy, allocationToDestroy);
            });
    }

    // ---- BRDF LUT 生成（不依赖 skybox） ----
    void VulkanIBLGenerator::GenerateBRDFLut()
    {
        auto* ctx    = VulkanContext::Get();
        auto  device = ctx->GetDevice();

        // 释放上一次（如有）
        DestroyImage(m_BRDFLut);
        m_BRDFLut     = CreateStorageImage2D(BRDF_LUT_SIZE, BRDF_LUT_SIZE, VK_FORMAT_R16G16_SFLOAT);
        m_BRDFLutView = m_BRDFLut.View;

        // 分配 + 写 descriptor set
        VkDescriptorSet set = m_DescriptorPool->Allocate(m_BRDFLutSetLayout->GetHandle());
        ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE, "[VulkanIBL] BRDF LUT descriptor allocate failed");

        VulkanDescriptorWriter writer;
        writer.WriteImage(0, m_BRDFLut.View, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.UpdateSet(device, set);

        // 录制并阻塞提交（IBL 是一次性预计算）
        VkCommandBuffer     raw = ctx->BeginSingleTimeCommands();
        VulkanCommandBuffer cmd(raw);

        // UNDEFINED -> GENERAL 供 compute 写
        cmd.ImageBarrier(m_BRDFLut.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         VK_ACCESS_SHADER_WRITE_BIT);

        cmd.BindComputePipeline(m_BRDFLutPipeline.Pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_BRDFLutPipeline.Layout, 0, {set});

        const uint32_t groupsX = (BRDF_LUT_SIZE + 15) / 16;
        const uint32_t groupsY = (BRDF_LUT_SIZE + 15) / 16;
        cmd.Dispatch(groupsX, groupsY, 1);

        // GENERAL -> SHADER_READ_ONLY_OPTIMAL 供 PBR 采样
        cmd.ImageBarrier(m_BRDFLut.Image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        ctx->EndSingleTimeCommands(raw);

        ENGINE_CORE_INFO("[VulkanIBL] BRDF LUT generated ({}x{})", BRDF_LUT_SIZE, BRDF_LUT_SIZE);
    }

    // ---- Irradiance + Prefilter 生成（依赖 skybox cubemap） ----
    void VulkanIBLGenerator::Generate(const Ref<TextureCubemap>& skybox)
    {
        if (!skybox)
            return;

        auto* ctx    = VulkanContext::Get();
        auto  device = ctx->GetDevice();

        // VulkanTextureCubemap 现已实装 (commit 0c6eb28)，dynamic_pointer_cast 拿 cube view + sampler
        auto vkCube = std::dynamic_pointer_cast<VulkanTextureCubemap>(skybox);
        if (!vkCube || vkCube->GetImageView() == VK_NULL_HANDLE)
        {
            ENGINE_CORE_WARN("[VulkanIBL] Skybox is not a VulkanTextureCubemap or has null image view; "
                             "Irradiance/Prefilter generation skipped");
            return;
        }

        const int envFaceSize = static_cast<int>(skybox->GetWidth());
        if (envFaceSize <= 0)
        {
            ENGINE_CORE_WARN("[VulkanIBL] Skybox face size = 0; Irradiance/Prefilter generation skipped");
            return;
        }

        VkImageView envCubeView    = vkCube->GetImageView();
        VkSampler   envCubeSampler = vkCube->GetSampler();

        // ---- 1) Irradiance ----
        DestroyImage(m_Irradiance);
        m_Irradiance     = CreateStorageImage2D(IRRADIANCE_SIZE * 6, IRRADIANCE_SIZE, VK_FORMAT_R16G16B16A16_SFLOAT);
        m_IrradianceView = m_Irradiance.View;

        VkDescriptorSet irrSet = m_DescriptorPool->Allocate(m_IrradianceSetLayout->GetHandle());
        ENGINE_CORE_RELEASE_ASSERT(irrSet != VK_NULL_HANDLE, "[VulkanIBL] Irradiance descriptor allocate failed");
        {
            VulkanDescriptorWriter w;
            w.WriteImage(0, m_Irradiance.View, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            w.WriteImage(1, envCubeView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, envCubeSampler);
            w.UpdateSet(device, irrSet);
        }

        // ---- 2) Prefilter（仅 mip0；mip 链留给后续 Vulkan PBR 接入时再补） ----
        DestroyImage(m_Prefilter);
        m_Prefilter     = CreateStorageImage2D(PREFILTER_SIZE * 6, PREFILTER_SIZE, VK_FORMAT_R16G16B16A16_SFLOAT);
        m_PrefilterView = m_Prefilter.View;

        VkDescriptorSet preSet = m_DescriptorPool->Allocate(m_PrefilterSetLayout->GetHandle());
        ENGINE_CORE_RELEASE_ASSERT(preSet != VK_NULL_HANDLE, "[VulkanIBL] Prefilter descriptor allocate failed");
        {
            VulkanDescriptorWriter w;
            w.WriteImage(0, m_Prefilter.View, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            w.WriteImage(1, envCubeView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, envCubeSampler);
            w.UpdateSet(device, preSet);
        }

        VkCommandBuffer     raw = ctx->BeginSingleTimeCommands();
        VulkanCommandBuffer cmd(raw);

        // 输出 image 转到 GENERAL；envCube 已在 VulkanTextureCubemap 构造时转为 SHADER_READ_ONLY_OPTIMAL
        cmd.ImageBarrier(m_Irradiance.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         VK_ACCESS_SHADER_WRITE_BIT);
        cmd.ImageBarrier(m_Prefilter.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         VK_ACCESS_SHADER_WRITE_BIT);

        // ---- Irradiance dispatch ----
        cmd.BindComputePipeline(m_IrradiancePipeline.Pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_IrradiancePipeline.Layout, 0, {irrSet});

        struct IrradiancePC
        {
            int32_t FaceSize;
            int32_t EnvFaceSize;
        } irrPC{IRRADIANCE_SIZE, envFaceSize};
        cmd.PushConstants(m_IrradiancePipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(irrPC), &irrPC);

        {
            const uint32_t gx = (IRRADIANCE_SIZE * 6 + 15) / 16;
            const uint32_t gy = (IRRADIANCE_SIZE + 15) / 16;
            cmd.Dispatch(gx, gy, 1);
        }

        // Irradiance 输出 -> SHADER_READ_ONLY_OPTIMAL
        cmd.ImageBarrier(m_Irradiance.Image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        // ---- Prefilter dispatch（用 roughness=0.5 作为单 mip 的代表；mip 链待后续补） ----
        cmd.BindComputePipeline(m_PrefilterPipeline.Pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, m_PrefilterPipeline.Layout, 0, {preSet});

        struct PrefilterPC
        {
            int32_t FaceSize;
            int32_t EnvFaceSize;
            float   Roughness;
        } prePC{PREFILTER_SIZE, envFaceSize, 0.5f};
        cmd.PushConstants(m_PrefilterPipeline.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(prePC), &prePC);

        {
            const uint32_t gx = (PREFILTER_SIZE * 6 + 15) / 16;
            const uint32_t gy = (PREFILTER_SIZE + 15) / 16;
            cmd.Dispatch(gx, gy, 1);
        }

        cmd.ImageBarrier(m_Prefilter.Image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        ctx->EndSingleTimeCommands(raw);

        m_IBLReady = true;
        ENGINE_CORE_INFO("[VulkanIBL] Irradiance + Prefilter generated (env cubemap {}x{})", envFaceSize, envFaceSize);
    }

} // namespace Engine
