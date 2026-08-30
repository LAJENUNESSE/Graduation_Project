#include "engpch.h"
#include "Platform/Vulkan/VulkanSceneDrawDispatcher.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanAllocator.h"

#include <vma/vk_mem_alloc.h>
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDescriptor.h"
#include "Platform/Vulkan/VulkanGraphicsPipelineBuilder.h"
#include "Platform/Vulkan/VulkanSceneState.h"
#include "Platform/Vulkan/VulkanVertexArray.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <glm/glm.hpp>

namespace Engine
{

    namespace
    {
        // ---- std140 打包结构：与 PBR.glsl Vulkan 分支布局逐字节对齐 ----

        struct GlobalUboStd140
        {
            glm::mat4 ViewProjection;               //   0
            glm::mat4 LightSpaceMatrix;             //  64
            glm::mat4 ViewMatrix;                   // 128
            glm::vec3 ViewPos;                      // 192
            float     _Pad0;                        // 204
            glm::mat4 CascadeLightSpaceMatrices[4]; // 208
            // std140 数组 stride=16B：C++ float[4]=16B，各补 48B 到 64B 槽位
            float   CascadeSplitDepths[4]; // 464
            float   _Pad1[12];
            float   CascadeTexelWorldSize[4]; // 528
            float   _Pad2[12];
            int32_t NumDirLights; // 592
            int32_t NumPointLights;
            int32_t NumSpotLights;
            int32_t CascadeCount;
            int32_t ShadowEnabled; // 608
            int32_t CSMEnabled;
            int32_t IBLEnabled;
            int32_t IBLDebugMode;
            int32_t SSAOEnabled; // 624
            float   AmbientStrength;
            float   IBLIntensity;
            float   ShadowBias; // 636
        };
        static_assert(sizeof(GlobalUboStd140) == 640, "GlobalUBO std140 layout mismatch");

        struct DirLightStd140
        {
            glm::vec3 Direction;
            float     _Pad0;
            glm::vec3 Color;
            float     Intensity;
        };
        static_assert(sizeof(DirLightStd140) == 32, "DirLight std140 size");
        static_assert(offsetof(DirLightStd140, Direction) == 0 && offsetof(DirLightStd140, Color) == 16 &&
                          offsetof(DirLightStd140, Intensity) == 28,
                      "DirLight std140 offsets");

        struct PointLightStd140
        {
            glm::vec3 Position;
            float     _Pad0;
            glm::vec3 Color;
            float     Intensity, Constant, Linear, Quadratic;
            float     _Pad1;
        };
        static_assert(sizeof(PointLightStd140) == 48, "PointLight std140 size");
        static_assert(offsetof(PointLightStd140, Position) == 0 && offsetof(PointLightStd140, Color) == 16 &&
                          offsetof(PointLightStd140, Intensity) == 28 && offsetof(PointLightStd140, Constant) == 32 &&
                          offsetof(PointLightStd140, Linear) == 36 && offsetof(PointLightStd140, Quadratic) == 40,
                      "PointLight std140 offsets");

        struct SpotLightStd140
        {
            glm::vec3 Position;
            float     _Pad0;
            glm::vec3 Direction;
            float     _Pad1;
            glm::vec3 Color;
            float     Intensity, Constant, Linear, Quadratic, InnerCutoff, OuterCutoff; // @48..72
            float     _Pad2[3]; // struct 向上取整到 16B 倍数 → 80
        };
        static_assert(sizeof(SpotLightStd140) == 80, "SpotLight std140 size");
        static_assert(offsetof(SpotLightStd140, Position) == 0 && offsetof(SpotLightStd140, Direction) == 16 &&
                          offsetof(SpotLightStd140, Color) == 32 && offsetof(SpotLightStd140, Intensity) == 44 &&
                          offsetof(SpotLightStd140, Constant) == 48 && offsetof(SpotLightStd140, Linear) == 52 &&
                          offsetof(SpotLightStd140, Quadratic) == 56 && offsetof(SpotLightStd140, InnerCutoff) == 60 &&
                          offsetof(SpotLightStd140, OuterCutoff) == 64,
                      "SpotLight std140 offsets");

        struct LightsUboStd140
        {
            DirLightStd140   DirLights[2];   //   0
            PointLightStd140 PointLights[8]; //  64
            SpotLightStd140  SpotLights[4];  // 448
        };
        static_assert(sizeof(LightsUboStd140) == 768, "LightsUBO std140 size");

        static_assert(sizeof(GlobalUboStd140) == VulkanSceneDrawDispatcher::kGlobalUboSize,
                      "GlobalUBO constant does not match its CPU layout");
        static_assert(sizeof(LightsUboStd140) == VulkanSceneDrawDispatcher::kLightsUboSize,
                      "LightsUBO constant does not match its CPU layout");

        bool IsFiniteMat4(const glm::mat4& matrix)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row]))
                        return false;
            return true;
        }

        float MaxAbsMat4(const glm::mat4& matrix)
        {
            float maxAbs = 0.0f;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    maxAbs = std::max(maxAbs, std::abs(matrix[column][row]));
            return maxAbs;
        }

        struct MaterialUboStd140
        {
            glm::vec4 Color;      //  0
            glm::vec2 Tiling;     // 16
            float     Metallic;   // 24
            float     Roughness;  // 28
            int32_t   HasTexture; // 32
            int32_t   HasNormalMap;
            int32_t   HasMetallicMap;
            int32_t   HasRoughnessMap;
            int32_t   HasAOMap;
            int32_t   EntityID; // 52
            int32_t   _Pad[2];  // 56, 60 (std140 block size = 64)
        };
        static_assert(sizeof(MaterialUboStd140) == 64, "MaterialUBO std140 size");

        struct ScenePCStd140
        {
            glm::mat4 Transform;       //  0
            glm::vec4 NormalMatrix[3]; // 64：std430 mat3 列各占 16B
        };
        static_assert(sizeof(ScenePCStd140) == 112, "Scene push constant size");

        // GLSL sampler uniform 名 → OpenGL texture unit（与 PBR.glsl / Skybox.glsl 约定一致）
        uint32_t SlotForSamplerName(std::string name)
        {
            // 去掉数组元素后缀（反射名可能带 "[0]"）
            const size_t bracket = name.find('[');
            if (bracket != std::string::npos)
                name.resize(bracket);

            static const std::unordered_map<std::string, uint32_t> table = {
                {"u_DiffuseTexture", 0},
                {"u_ShadowMap", 1},
                {"u_NormalMap", 2},
                {"u_MetallicMap", 3},
                {"u_RoughnessMap", 4},
                {"u_AOMap", 5},
                {"u_IrradianceMap", 6},
                {"u_PrefilterMap", 7},
                {"u_BRDF_LUT", 8},
                {"u_SSAOTexture", 9},
                {"u_Skybox", 0},
                {"u_HDRBuffer", 0},
                {"u_BloomBlur", 15},
                {"u_GrassTexture", 12},
                {"u_Splatmap", 6},
                // CSM 数组基址：SceneRenderer 把级联深度 view 绑在 slots 10~13
                {"u_CascadeShadowMaps", 10},
            };
            auto it = table.find(name);
            return it != table.end() ? it->second : UINT32_MAX;
        }

        // samplerCube 类型的 GLSL uniform 名：占位 view 必须用 CUBE 型，
        // 2D view 写进 samplerCube binding 在部分驱动直接 device lost
        bool IsCubeSamplerName(const std::string& name)
        {
            static const std::array<const char*, 3> kCubeNames = {"u_IrradianceMap", "u_PrefilterMap", "u_Skybox"};
            for (const char* cube : kCubeNames)
                if (name == cube)
                    return true;
            return false;
        }

    } // namespace

    void VulkanSceneDrawDispatcher::Init(VkDevice device)
    {
        ENGINE_CORE_RELEASE_ASSERT(device != VK_NULL_HANDLE, "VulkanSceneDrawDispatcher requires a device");
        m_Device = device;

        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
            CreateFrameResources(i);

        CreatePlaceholder();

        m_Initialized = true;
        ENGINE_CORE_INFO("[Vulkan] Scene draw dispatcher initialized");
    }

    void VulkanSceneDrawDispatcher::CreateFrameResources(uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];

        // ---- global buffer（Global + Lights 两段，host-visible persistent mapped）----
        {
            const VkDeviceSize totalSize = static_cast<VkDeviceSize>(kGlobalRingStride) * kMaxGlobalAllocsPerFrame;

            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size  = totalSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo outInfo{};
            const VkResult    result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo,
                                                       &fr.GlobalBuffer, &fr.GlobalAllocation, &outInfo);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create scene global UBO buffer");
            ENGINE_CORE_RELEASE_ASSERT(outInfo.pMappedData != nullptr, "Scene global UBO mapping returned null");
            fr.GlobalMapped = outInfo.pMappedData;
        }

        // ---- material ring ----
        {
            const VkDeviceSize totalSize = static_cast<VkDeviceSize>(kMaterialUboSize) * kMaxMaterialAllocsPerFrame;

            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size  = totalSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo outInfo{};
            const VkResult    result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo,
                                                       &fr.MaterialBuffer, &fr.MaterialAllocation, &outInfo);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create scene material ring buffer");
            ENGINE_CORE_RELEASE_ASSERT(outInfo.pMappedData != nullptr, "Scene material UBO mapping returned null");
            fr.MaterialMapped = outInfo.pMappedData;
        }

        // ---- per-frame descriptor pool（P-15 惯例：帧首 Reset）----
        {
            std::vector<VkDescriptorPoolSize> sizes = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxMaterialAllocsPerFrame * 3},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterialAllocsPerFrame * 8},
            };

            VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets       = kMaxMaterialAllocsPerFrame * 2;
            poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
            poolInfo.pPoolSizes    = sizes.data();

            const VkResult result = vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &fr.Pool);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create scene descriptor pool");
        }
    }

    void VulkanSceneDrawDispatcher::Shutdown(VkDevice device)
    {
        if (!m_Initialized)
            return;

        vkDeviceWaitIdle(device);
        DestroyPlaceholder();
        for (auto& fr : m_Frames)
        {
            if (fr.Pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device, fr.Pool, nullptr);
                fr.Pool = VK_NULL_HANDLE;
            }
            if (fr.GlobalAllocation)
            {
                vmaDestroyBuffer(VulkanAllocator::GetAllocator(), fr.GlobalBuffer, fr.GlobalAllocation);
                fr.GlobalBuffer     = VK_NULL_HANDLE;
                fr.GlobalAllocation = nullptr;
            }
            if (fr.MaterialAllocation)
            {
                vmaDestroyBuffer(VulkanAllocator::GetAllocator(), fr.MaterialBuffer, fr.MaterialAllocation);
                fr.MaterialBuffer     = VK_NULL_HANDLE;
                fr.MaterialAllocation = nullptr;
            }
        }
        m_Initialized = false;
    }

    void VulkanSceneDrawDispatcher::OnBeginFrame(uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];
        fr.GlobalOffset    = 0;
        fr.MaterialOffset  = 0;
        if (fr.Pool)
            vkResetDescriptorPool(m_Device, fr.Pool, 0);
    }

    void VulkanSceneDrawDispatcher::CreatePlaceholder()
    {
        constexpr uint32_t kSize   = 1;
        constexpr uint32_t kLayers = 6; // cube 占位需要 6 层
        const VkFormat     format  = VK_FORMAT_R8G8B8A8_UNORM;

        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required for placeholder texture");

        // 两张独立 1x1 图：纯 2D（单层）与 CUBE_COMPATIBLE（6 层）。
        // combined image sampler 的 view 类型必须匹配 shader 声明——samplerCube
        // 绑 2D view 在部分驱动直接 device lost。
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.format        = format;
        imageInfo.extent        = {kSize, kSize, 1};
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageCreateInfo cubeInfo = imageInfo;
        cubeInfo.arrayLayers       = kLayers;
        cubeInfo.flags             = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VkResult result = vmaCreateImage(VulkanAllocator::GetAllocator(), &imageInfo, &allocInfo,
                                         &m_Placeholder.Image2D, &m_Placeholder.Allocation2D, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create placeholder 2D image");

        result = vmaCreateImage(VulkanAllocator::GetAllocator(), &cubeInfo, &allocInfo, &m_Placeholder.ImageCube,
                                &m_Placeholder.AllocationCube, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create placeholder cube image");

        // ---- 上传白色像素（2D 一层 / cube 六层，共用 staging）----
        const VkDeviceSize stagingSize = sizeof(uint32_t) * kLayers;

        VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        stagingInfo.size  = stagingSize;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAlloc{};
        stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkBuffer      stagingBuffer      = VK_NULL_HANDLE;
        VmaAllocation stagingAllocHandle = nullptr;
        result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &stagingInfo, &stagingAlloc, &stagingBuffer,
                                 &stagingAllocHandle, nullptr);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create placeholder staging buffer");

        void* mapped = nullptr;
        vmaMapMemory(VulkanAllocator::GetAllocator(), stagingAllocHandle, &mapped);
        for (uint32_t i = 0; i < kLayers; ++i)
            static_cast<uint32_t*>(mapped)[i] = 0xFFFFFFFFu;
        vmaUnmapMemory(VulkanAllocator::GetAllocator(), stagingAllocHandle);

        VkCommandBuffer     cmd = context->BeginSingleTimeCommands();
        VulkanCommandBuffer wrapper(cmd);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageExtent                 = {kSize, kSize, 1};

        struct UploadTarget
        {
            VkImage       Image;
            VmaAllocation Allocation;
            uint32_t      Layers;
        };
        const UploadTarget targets[2] = {{m_Placeholder.Image2D, m_Placeholder.Allocation2D, 1},
                                         {m_Placeholder.ImageCube, m_Placeholder.AllocationCube, kLayers}};

        for (const auto& target : targets)
        {
            region.imageSubresource.layerCount = target.Layers;

            wrapper.ImageBarrier(target.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, target.Layers);
            vkCmdCopyBufferToImage(cmd, stagingBuffer, target.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            wrapper.ImageBarrier(target.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, target.Layers);
        }
        context->EndSingleTimeCommands(cmd);

        vmaDestroyBuffer(VulkanAllocator::GetAllocator(), stagingBuffer, stagingAllocHandle);

        // ---- views：2D（单层）+ CUBE（6 层）----
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.format                      = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;

        viewInfo.image                       = m_Placeholder.Image2D;
        viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.subresourceRange.layerCount = 1;
        result                               = vkCreateImageView(m_Device, &viewInfo, nullptr, &m_Placeholder.View2D);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create placeholder 2D view");

        viewInfo.image                       = m_Placeholder.ImageCube;
        viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.subresourceRange.layerCount = kLayers;
        result                               = vkCreateImageView(m_Device, &viewInfo, nullptr, &m_Placeholder.ViewCube);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create placeholder cube view");
    }

    void VulkanSceneDrawDispatcher::DestroyPlaceholder()
    {
        if (m_Placeholder.View2D != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_Device, m_Placeholder.View2D, nullptr);
            m_Placeholder.View2D = VK_NULL_HANDLE;
        }
        if (m_Placeholder.ViewCube != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_Device, m_Placeholder.ViewCube, nullptr);
            m_Placeholder.ViewCube = VK_NULL_HANDLE;
        }
        if (m_Placeholder.Image2D != VK_NULL_HANDLE)
        {
            vmaDestroyImage(VulkanAllocator::GetAllocator(), m_Placeholder.Image2D, m_Placeholder.Allocation2D);
            m_Placeholder.Image2D      = VK_NULL_HANDLE;
            m_Placeholder.Allocation2D = nullptr;
        }
        if (m_Placeholder.ImageCube != VK_NULL_HANDLE)
        {
            vmaDestroyImage(VulkanAllocator::GetAllocator(), m_Placeholder.ImageCube, m_Placeholder.AllocationCube);
            m_Placeholder.ImageCube      = VK_NULL_HANDLE;
            m_Placeholder.AllocationCube = nullptr;
        }
    }

    uint32_t VulkanSceneDrawDispatcher::PackAndUploadGlobals(VulkanShader* shader, uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];
        if (fr.GlobalOffset + kGlobalRingStride > kGlobalRingStride * kMaxGlobalAllocsPerFrame)
        {
            static bool warnedFull = false;
            if (!warnedFull)
            {
                warnedFull = true;
                ENGINE_CORE_WARN("[Vulkan] Global UBO ring exhausted this frame; draws dropped");
            }
            return UINT32_MAX;
        }

        const uint32_t globalOffset = fr.GlobalOffset;
        fr.GlobalOffset += kGlobalRingStride;

        // 每个 draw 使用独立的 CPU 临时结构，避免后续 shader/帧覆盖已录制命令
        // 所引用的全局 UBO 段。
        GlobalUboStd140 globalData{};
        LightsUboStd140 lightsData{};

        const auto& mat4s = shader->GetMat4Uniforms();
        const auto& vec3s = shader->GetFloat3Uniforms();
        const auto& ints  = shader->GetIntUniforms();
        const auto& flts  = shader->GetFloatUniforms();

        auto readMat4 = [&](const char* name, glm::mat4& out)
        {
            auto it = mat4s.find(name);
            if (it != mat4s.end())
                out = it->second;
        };
        auto readVec3 = [&](const char* name, glm::vec3& out)
        {
            auto it = vec3s.find(name);
            if (it != vec3s.end())
                out = it->second;
        };
        auto readInt = [&](const char* name, int32_t& out)
        {
            auto it = ints.find(name);
            if (it != ints.end())
                out = it->second;
        };
        auto readFloat = [&](const char* name, float& out)
        {
            auto it = flts.find(name);
            if (it != flts.end())
                out = it->second;
        };

        // ---- globals ----
        readMat4("u_ViewProjection", globalData.ViewProjection);
        readMat4("u_LightSpaceMatrix", globalData.LightSpaceMatrix);
        readMat4("u_ViewMatrix", globalData.ViewMatrix);
        readVec3("u_ViewPos", globalData.ViewPos);

        bool hasGlobalUbo = false;
        for (const auto& binding : shader->GetReflectedBindings())
        {
            if (binding.Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && binding.Name.find("Global") != std::string::npos)
            {
                hasGlobalUbo = true;
                break;
            }
        }

        const auto vpIt = mat4s.find("u_ViewProjection");
        if (hasGlobalUbo && (vpIt == mat4s.end() || !IsFiniteMat4(globalData.ViewProjection) ||
                             MaxAbsMat4(globalData.ViewProjection) < 1.0e-6f))
        {
            static std::unordered_set<const VulkanShader*> reported;
            if (reported.insert(shader).second)
                ENGINE_CORE_ERROR("[Vulkan UBO] shader '{}' has an invalid/zero u_ViewProjection before upload",
                                  shader->GetName());
        }

        // GL 惯例投影的 clip z∈[-1,1]，Vulkan 裁剪体积 z∈[0,1]——对 z 行做
        // z' = 0.5*z + 0.5*w 重映射（列主序：每列的 row2 与 row3 线性组合）。
        // 只影响 Vulkan 路径；GL 后端读的是同一份 CPU uniform 不经过这里。
        const auto remapDepthToVulkan = [](glm::mat4& m)
        {
            for (int c = 0; c < 4; ++c)
                m[c][2] = 0.5f * m[c][2] + 0.5f * m[c][3];
        };
        remapDepthToVulkan(globalData.ViewProjection);
        remapDepthToVulkan(globalData.LightSpaceMatrix);
        for (int i = 0; i < 4; ++i)
        {
            readMat4(("u_CascadeLightSpaceMatrices[" + std::to_string(i) + "]").c_str(),
                     globalData.CascadeLightSpaceMatrices[i]);
            remapDepthToVulkan(globalData.CascadeLightSpaceMatrices[i]);
            readFloat(("u_CascadeSplitDepths[" + std::to_string(i) + "]").c_str(), globalData.CascadeSplitDepths[i]);
            readFloat(("u_CascadeTexelWorldSize[" + std::to_string(i) + "]").c_str(),
                      globalData.CascadeTexelWorldSize[i]);
        }
        readInt("u_NumDirLights", globalData.NumDirLights);
        readInt("u_NumPointLights", globalData.NumPointLights);
        readInt("u_NumSpotLights", globalData.NumSpotLights);
        readInt("u_CascadeCount", globalData.CascadeCount);
        readInt("u_ShadowEnabled", globalData.ShadowEnabled);
        readInt("u_CSMEnabled", globalData.CSMEnabled);
        readInt("u_IBLEnabled", globalData.IBLEnabled);
        readInt("u_IBLDebugMode", globalData.IBLDebugMode);
        readInt("u_SSAOEnabled", globalData.SSAOEnabled);
        readFloat("u_AmbientStrength", globalData.AmbientStrength);
        readFloat("u_IBLIntensity", globalData.IBLIntensity);
        readFloat("u_ShadowBias", globalData.ShadowBias);

        if (hasGlobalUbo)
        {
            const int32_t originalDirCount   = globalData.NumDirLights;
            const int32_t originalPointCount = globalData.NumPointLights;
            const int32_t originalSpotCount  = globalData.NumSpotLights;
            globalData.NumDirLights          = std::clamp(globalData.NumDirLights, 0, 2);
            globalData.NumPointLights        = std::clamp(globalData.NumPointLights, 0, 8);
            globalData.NumSpotLights         = std::clamp(globalData.NumSpotLights, 0, 4);
            if (originalDirCount != globalData.NumDirLights || originalPointCount != globalData.NumPointLights ||
                originalSpotCount != globalData.NumSpotLights)
            {
                static bool reportedCount = false;
                if (!reportedCount)
                {
                    reportedCount = true;
                    ENGINE_CORE_WARN("[Vulkan UBO] clamped light counts to shader capacities: dir={} point={} spot={}",
                                     globalData.NumDirLights, globalData.NumPointLights, globalData.NumSpotLights);
                }
            }
        }

        // ---- lights（std140 结构体数组逐字段提取）----
        for (int i = 0; i < 2; ++i)
        {
            const std::string idx = "[" + std::to_string(i) + "]";
            auto&             dl  = lightsData.DirLights[i];
            readVec3(("u_DirLights" + idx + ".direction").c_str(), dl.Direction);
            readVec3(("u_DirLights" + idx + ".color").c_str(), dl.Color);
            readFloat(("u_DirLights" + idx + ".intensity").c_str(), dl.Intensity);
        }
        for (int i = 0; i < 8; ++i)
        {
            const std::string idx = "[" + std::to_string(i) + "]";
            auto&             pl  = lightsData.PointLights[i];
            readVec3(("u_PointLights" + idx + ".position").c_str(), pl.Position);
            readVec3(("u_PointLights" + idx + ".color").c_str(), pl.Color);
            readFloat(("u_PointLights" + idx + ".intensity").c_str(), pl.Intensity);
            readFloat(("u_PointLights" + idx + ".constant").c_str(), pl.Constant);
            readFloat(("u_PointLights" + idx + ".linear").c_str(), pl.Linear);
            readFloat(("u_PointLights" + idx + ".quadratic").c_str(), pl.Quadratic);
        }
        for (int i = 0; i < 4; ++i)
        {
            const std::string idx = "[" + std::to_string(i) + "]";
            auto&             sl  = lightsData.SpotLights[i];
            readVec3(("u_SpotLights" + idx + ".position").c_str(), sl.Position);
            readVec3(("u_SpotLights" + idx + ".direction").c_str(), sl.Direction);
            readVec3(("u_SpotLights" + idx + ".color").c_str(), sl.Color);
            readFloat(("u_SpotLights" + idx + ".intensity").c_str(), sl.Intensity);
            readFloat(("u_SpotLights" + idx + ".constant").c_str(), sl.Constant);
            readFloat(("u_SpotLights" + idx + ".linear").c_str(), sl.Linear);
            readFloat(("u_SpotLights" + idx + ".quadratic").c_str(), sl.Quadratic);
            readFloat(("u_SpotLights" + idx + ".innerCutoff").c_str(), sl.InnerCutoff);
            readFloat(("u_SpotLights" + idx + ".outerCutoff").c_str(), sl.OuterCutoff);
        }

        auto* mapped = static_cast<uint8_t*>(fr.GlobalMapped);
        ENGINE_CORE_RELEASE_ASSERT(mapped != nullptr, "Scene global UBO mapped pointer is null");
        std::memcpy(mapped + globalOffset, &globalData, kGlobalUboSize);
        std::memcpy(mapped + globalOffset + kLightsUboOffset, &lightsData, kLightsUboSize);

        // HOST_VISIBLE 非 coherent 内存写后必须 flush，GPU 才能看到。两个 payload
        // 分别 flush，避免把 ring 中未使用的 padding 当成有效范围。
        const VkResult globalFlush =
            vmaFlushAllocation(VulkanAllocator::GetAllocator(), fr.GlobalAllocation, globalOffset, kGlobalUboSize);
        const VkResult lightsFlush = vmaFlushAllocation(VulkanAllocator::GetAllocator(), fr.GlobalAllocation,
                                                        globalOffset + kLightsUboOffset, kLightsUboSize);
        ENGINE_CORE_RELEASE_ASSERT(globalFlush == VK_SUCCESS && lightsFlush == VK_SUCCESS,
                                   "Failed to flush scene global UBO allocation");

        if (hasGlobalUbo)
        {
            static std::unordered_set<const VulkanShader*> logged;
            if (logged.insert(shader).second)
            {
                const auto& vp = globalData.ViewProjection;
                ENGINE_CORE_INFO("[Vulkan UBO] shader='{}' allocation={} globalOffset={} globalSize={} lightsOffset={} "
                                 "lightsSize={} "
                                 "vpCol0=({}, {}, {}, {})",
                                 shader->GetName(), static_cast<const void*>(fr.GlobalAllocation), globalOffset,
                                 kGlobalUboSize, globalOffset + kLightsUboOffset, kLightsUboSize, vp[0][0], vp[0][1],
                                 vp[0][2], vp[0][3]);
            }
        }

        return globalOffset;
    }

    uint32_t VulkanSceneDrawDispatcher::PackMaterial(VulkanShader* shader, uint32_t frameIndex)
    {
        FrameResources& fr = m_Frames[frameIndex];
        if (fr.MaterialOffset + kMaterialRingStride > kMaterialRingStride * kMaxMaterialAllocsPerFrame)
        {
            static bool warnedFull = false;
            if (!warnedFull)
            {
                warnedFull = true;
                ENGINE_CORE_WARN("[Vulkan] Material ring exhausted this frame; draws dropped");
            }
            return UINT32_MAX;
        }

        auto* m = reinterpret_cast<MaterialUboStd140*>(static_cast<uint8_t*>(fr.MaterialMapped) + fr.MaterialOffset);
        *m      = {};

        const auto& vec4s = shader->GetFloat4Uniforms();
        const auto& vec2s = shader->GetFloat2Uniforms();
        const auto& flts  = shader->GetFloatUniforms();
        const auto& ints  = shader->GetIntUniforms();

        if (auto it = vec4s.find("u_Color"); it != vec4s.end())
            m->Color = it->second;
        if (auto it = vec2s.find("u_Tiling"); it != vec2s.end())
            m->Tiling = it->second;
        if (auto it = flts.find("u_Metallic"); it != flts.end())
            m->Metallic = it->second;
        if (auto it = flts.find("u_Roughness"); it != flts.end())
            m->Roughness = it->second;
        if (auto it = ints.find("u_HasTexture"); it != ints.end())
            m->HasTexture = it->second;
        if (auto it = ints.find("u_HasNormalMap"); it != ints.end())
            m->HasNormalMap = it->second;
        if (auto it = ints.find("u_HasMetallicMap"); it != ints.end())
            m->HasMetallicMap = it->second;
        if (auto it = ints.find("u_HasRoughnessMap"); it != ints.end())
            m->HasRoughnessMap = it->second;
        if (auto it = ints.find("u_HasAOMap"); it != ints.end())
            m->HasAOMap = it->second;
        if (auto it = ints.find("u_EntityID"); it != ints.end())
            m->EntityID = it->second;

        const uint32_t offset = fr.MaterialOffset;
        fr.MaterialOffset += kMaterialRingStride; // 256 对齐（descriptor offset 规范要求）

        // 同 PackAndUploadGlobals：非 coherent host 内存写后必须 flush
        vmaFlushAllocation(VulkanAllocator::GetAllocator(), fr.MaterialAllocation, offset, kMaterialUboSize);
        return offset;
    }

    bool VulkanSceneDrawDispatcher::DispatchDraw(const VertexArray* vertexArray,
                                                 VulkanShader*      shader,
                                                 const DrawParams&  params,
                                                 uint32_t           frameIndex)
    {
        if (!m_Initialized || !shader || !vertexArray || params.RenderPass == VK_NULL_HANDLE ||
            params.Cmd == VK_NULL_HANDLE)
            return false;

        auto* va = dynamic_cast<const VulkanVertexArray*>(vertexArray);
        if (!va || va->GetVertexBuffers().empty())
            return false;

        auto* context = VulkanContext::Get();
        if (!context)
            return false;

        FrameResources& fr = m_Frames[frameIndex];

        // ---- pipeline ----
        GraphicsPipelineDesc desc{};
        desc.Shader               = shader;
        desc.RenderPass           = params.RenderPass;
        desc.ColorAttachmentCount = params.ColorAttachmentCount;
        desc.Bindings             = va->BuildBindingDescriptions();
        desc.Attributes           = va->BuildAttributeDescriptions();
        desc.DepthTest            = params.DepthTest;
        desc.DepthWrite           = params.DepthWrite;
        desc.DepthLEqual          = params.DepthLEqual;
        desc.CullBack             = params.CullBack;

        VulkanPipelineCache::PipelineHandle handle = context->GetPipelineBuilder().GetOrCreate(m_Device, desc);
        if (handle.Pipeline == VK_NULL_HANDLE)
            return false;

        // ---- uniforms ----
        const uint32_t globalOffset = PackAndUploadGlobals(shader, frameIndex);
        if (globalOffset == UINT32_MAX)
            return false;
        const uint32_t materialOffset = PackMaterial(shader, frameIndex);
        if (materialOffset == UINT32_MAX)
            return false;

        // ---- descriptor sets：按反射 binding 分派写入（layout 复用 builder 缓存）----
        const auto& bindings = shader->GetReflectedBindings();

        uint32_t maxSet = 0;
        for (const auto& b : bindings)
            maxSet = std::max(maxSet, b.Set);

        const auto* cachedLayouts = context->GetPipelineBuilder().GetSetLayouts(shader);
        if (!cachedLayouts || cachedLayouts->size() <= maxSet)
            return false;

        std::array<VkDescriptorSet, 2> sets{};
        for (uint32_t set = 0; set <= maxSet && set < 2; ++set)
        {
            VkDescriptorSetLayout             layoutHandle = (*cachedLayouts)[set]->GetHandle();
            const VkDescriptorSetAllocateInfo setAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                                           fr.Pool, 1, &layoutHandle};
            VkDescriptorSet                   allocated = VK_NULL_HANDLE;
            const VkResult allocResult                  = vkAllocateDescriptorSets(m_Device, &setAllocInfo, &allocated);
            if (allocResult != VK_SUCCESS || allocated == VK_NULL_HANDLE)
                return false;
            sets[set] = allocated;
        }

        const VulkanSceneState& scene = context->GetSceneState();

        VulkanDescriptorWriter w0;
        VulkanDescriptorWriter w1;
        bool                   hasSet1 = (maxSet >= 1);

        for (const auto& b : bindings)
        {
            const std::string baseName = [&]
            {
                std::string  n          = b.Name;
                const size_t bracketPos = n.find('[');
                if (bracketPos != std::string::npos)
                    n.resize(bracketPos);
                return n;
            }();

            if (b.Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            {
                if (baseName.find("Global") != std::string::npos)
                    (b.Set == 0 ? w0 : w1)
                        .WriteBuffer(b.Binding, fr.GlobalBuffer, globalOffset, kGlobalUboSize, b.Type);
                else if (baseName.find("Lights") != std::string::npos)
                    (b.Set == 0 ? w0 : w1)
                        .WriteBuffer(b.Binding, fr.GlobalBuffer, globalOffset + kLightsUboOffset, kLightsUboSize,
                                     b.Type);
                else if (hasSet1 && b.Set == 1)
                    w1.WriteBuffer(b.Binding, fr.MaterialBuffer, materialOffset, kMaterialUboSize, b.Type);
            }
            else if (b.Type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                const uint32_t slotBase = SlotForSamplerName(baseName);
                if (slotBase == UINT32_MAX)
                    continue;

                for (uint32_t elem = 0; elem < b.Count; ++elem)
                {
                    const auto& slot = scene.GetTextureSlot(slotBase + elem);
                    // 未绑定槽位按 shader 声明类型选占位 view（samplerCube 必须 CUBE 型）
                    const VkImageView placeholderView =
                        IsCubeSamplerName(baseName) ? m_Placeholder.ViewCube : m_Placeholder.View2D;
                    if (!slot.Valid && placeholderView == VK_NULL_HANDLE)
                        continue;

                    // view 直通绑定（阴影图/FBO attachment）无自带 sampler → 用默认 sampler
                    const VkSampler sampler = slot.Sampler ? slot.Sampler : context->GetDefaultSampler();

                    const VkImageView writeView = slot.Valid ? slot.View : placeholderView;

                    // depth image 写入 descriptor 必须用 DEPTH_STENCIL_READ_ONLY_OPTIMAL
                    // （与 framebuffer finalLayout 匹配），普通 texture/cubemap 用 SHADER_READ_ONLY_OPTIMAL。
                    // layout 由 slot.Layout 携带（BindTextureView 写入时声明）。
                    VkImageLayout writeLayout = slot.Layout;
                    if (writeLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                        writeLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    if (b.Count > 1)
                        (b.Set == 0 ? w0 : w1)
                            .WriteImageElement(b.Binding, elem, writeView, writeLayout, b.Type, sampler);
                    else
                        (b.Set == 0 ? w0 : w1).WriteImage(b.Binding, writeView, writeLayout, b.Type, sampler);

                    // 诊断日志：每帧首 draw 输出 sampler binding 与实际写入 layout
                    static std::unordered_set<std::string> s_DbgLayoutLogged;
                    const std::string                      key = baseName + ":" + std::to_string(elem);
                    if (s_DbgLayoutLogged.insert(key).second)
                    {
                        const char* layoutName = "?";
                        switch (writeLayout)
                        {
                        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                            layoutName = "SHADER_READ_ONLY_OPTIMAL";
                            break;
                        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                            layoutName = "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
                            break;
                        default:
                            break;
                        }
                        ENGINE_CORE_WARN("[DbgBindLayout] base={0} elem={1} slot={2} layout={3} viewValid={4}",
                                         baseName, elem, slotBase + elem, layoutName, slot.Valid);
                    }
                }
            }
        }

        if (sets[0])
            w0.UpdateSet(m_Device, sets[0]);
        if (sets[1])
            w1.UpdateSet(m_Device, sets[1]);

        // ---- push constant（per-draw 变换）----
        // 仅当反射 PC 确实容纳 mat4 级别的 u_Transform（≥64B）时才写 ScenePCStd140；
        // 后处理等 shader 的 PC 布局不同（如 4 个标量共 16B），按 112B 写会越界污染
        const auto& pcs            = shader->GetReflectedPushConstants();
        const bool  hasTransformPC = std::any_of(pcs.begin(), pcs.end(), [](const auto& r) { return r.Size >= 64; });
        if (hasTransformPC)
        {
            ScenePCStd140 pcData{};
            if (auto it = shader->GetMat4Uniforms().find("u_Transform"); it != shader->GetMat4Uniforms().end())
                pcData.Transform = it->second;
            else if (auto vpIt = shader->GetMat4Uniforms().find("u_ViewProjection");
                     vpIt != shader->GetMat4Uniforms().end())
                pcData.Transform = vpIt->second; // Skybox 等 PC 首槽即视图投影的 shader（单 mat4 布局与首槽重合）
            if (auto it = shader->GetMat3Uniforms().find("u_NormalMatrix"); it != shader->GetMat3Uniforms().end())
            {
                // glm 列主序 mat3 → 3×vec16B 槽位
                for (int c = 0; c < 3; ++c)
                    pcData.NormalMatrix[c] = glm::vec4(it->second[c], 0.0f);
            }

            for (const auto& range : pcs)
                VulkanCommandBuffer(params.Cmd)
                    .PushConstants(handle.Layout, range.Stages, range.Offset, range.Size,
                                   reinterpret_cast<const uint8_t*>(&pcData) + range.Offset);
        }

        // ---- 录制 ----
        VulkanCommandBuffer cmd(params.Cmd);
        cmd.BindGraphicsPipeline(handle.Pipeline);

        // 防御性重录全尺寸动态 viewport/scissor：pipeline 使用 dynamic 状态，cmd 中
        // 残留的任何小 scissor/viewport（如半分辨率中间 pass 设置过）都会裁掉几何
        if (context->GetActiveSceneWidth() > 0 && context->GetActiveSceneHeight() > 0)
        {
            cmd.SetViewport(0, 0, static_cast<float>(context->GetActiveSceneWidth()),
                            static_cast<float>(context->GetActiveSceneHeight()));
            cmd.SetScissor(0, 0, context->GetActiveSceneWidth(), context->GetActiveSceneHeight());
        }

        std::vector<VkDescriptorSet> bindSets;
        for (uint32_t set = 0; set <= maxSet && set < 2; ++set)
            if (sets[set])
                bindSets.push_back(sets[set]);
        if (!bindSets.empty())
            cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, handle.Layout, 0, bindSets);

        // 顶点缓冲绑定（binding 号 = VAO 内 vertex buffer 下标）
        {
            const auto&           vbos = va->GetVertexBuffers();
            std::vector<VkBuffer> buffers;
            buffers.reserve(vbos.size());
            for (const auto& vbo : vbos)
                buffers.push_back(static_cast<const VulkanVertexBuffer*>(vbo.get())->GetBuffer());
            cmd.BindVertexBuffers(0, buffers);
        }

        if (params.Indexed)
        {
            const Ref<IndexBuffer>& ibo = va->GetIndexBuffer();
            if (!ibo)
                return false;
            cmd.BindIndexBuffer(static_cast<const VulkanIndexBuffer*>(ibo.get())->GetBuffer());
            cmd.DrawIndexed(params.IndexCount, 1, params.FirstIndex, params.VertexOffset);
        }
        else
        {
            cmd.Draw(params.VertexCount, 1, params.FirstVertex);
        }

        return true;
    }

} // namespace Engine
