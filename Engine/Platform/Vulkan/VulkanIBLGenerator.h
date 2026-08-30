#pragma once

#include "Renderer/IBLGenerator.h"
#include "Renderer/Shader.h"

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace Engine
{

    class VulkanShader;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;

    // IBL 预计算管线的 Vulkan 实现。
    // - 3 个 compute shader：BRDF_LUT / Irradiance / Prefilter（与 OpenGL 共享 .glsl，通过 #ifdef VULKAN 切换语法）
    // - uniform 通过 push constant 传递
    // - 输出 image 在 compute dispatch 期间布局为 GENERAL，之后转为 SHADER_READ_ONLY_OPTIMAL
    //
    // Irradiance/Prefilter 输出为 6-layer cube-compatible image（VULKAN shader 分支经
    // 2D_ARRAY view 写入各 layer，PBR 经 CUBE view 采样）；Prefilter 生成完整 mip 链，
    // 每 mip 一次 dispatch（roughness = mip / (mipLevels-1)）。
    class VulkanIBLGenerator : public IBLGenerator
    {
    public:
        VulkanIBLGenerator() = default;
        ~VulkanIBLGenerator() override { Shutdown(); }

        bool Init() override;
        void Generate(const Ref<TextureCubemap>& skybox) override;
        void Clear() override;
        void Shutdown() override;

        // GetXxxID 在 Vulkan 路径下不再是 OpenGL texture ID。
        // PBR pass 按 API 分派改走 GetXxxView() 接口（基类 void* 透传，避开 vulkan.h 泄漏 Engine/src/）。
        uint32_t GetIrradianceMapID() const override { return 0; }
        uint32_t GetPrefilterMapID() const override { return 0; }
        uint32_t GetBRDFLutID() const override { return 0; }
        bool     IsReady() const override { return m_IBLReady; }

        // 基类虚函数 override — 透传 VkImageView / VkSampler 为 void*。
        // SceneRenderer GeometryPass 按 RendererAPI::GetAPI() 分派调用 ID vs View。
        void* GetIrradianceView() const override { return reinterpret_cast<void*>(m_IrradianceView); }
        void* GetPrefilterView() const override { return reinterpret_cast<void*>(m_PrefilterView); }
        void* GetBRDFLutView() const override { return reinterpret_cast<void*>(m_BRDFLutView); }
        void* GetIBLSampler() const override { return reinterpret_cast<void*>(m_LinearSampler); }

        // 直接句柄访问（Vulkan 内部使用，避免 cast 来回）
        VkImageView GetBRDFLutViewHandle() const { return m_BRDFLutView; }
        VkImageView GetIrradianceViewHandle() const { return m_IrradianceView; }
        VkImageView GetPrefilterViewHandle() const { return m_PrefilterView; }
        VkSampler   GetSampler() const { return m_LinearSampler; }

    private:
        // 单独抽出的 BRDF LUT 生成；不依赖 skybox，在 Init 时即可调用
        void GenerateBRDFLut();

        // 工具：创建一张 storage_image 可写的 2D 纹理（VK_IMAGE_USAGE_STORAGE | SAMPLED）+ view
        struct ImageHandle
        {
            VkImage       Image      = VK_NULL_HANDLE;
            VmaAllocation Allocation = nullptr;
            VkImageView   View       = VK_NULL_HANDLE;
        };
        ImageHandle CreateStorageImage2D(uint32_t width, uint32_t height, VkFormat format);
        void        DestroyImage(ImageHandle& img);

        // 6-layer cube-compatible image：CUBE view 供 PBR samplerCube 采样（全 mip），
        // 每 mip 一个 2D_ARRAY view 供 compute storage 写入对应 mip。
        struct CubeImageHandle
        {
            VkImage                  Image      = VK_NULL_HANDLE;
            VmaAllocation            Allocation = nullptr;
            VkImageView              CubeView   = VK_NULL_HANDLE;
            std::vector<VkImageView> MipStorageViews;
        };
        CubeImageHandle CreateCubeStorageImage(uint32_t faceSize, VkFormat format, uint32_t mipLevels);
        void            DestroyCubeImage(CubeImageHandle& img);

        // shader + layout + pipeline 缓存（避免每次 dispatch 重建）
        Ref<VulkanShader>              m_BRDFLutShader;
        Ref<VulkanShader>              m_IrradianceShader;
        Ref<VulkanShader>              m_PrefilterShader;
        Ref<VulkanDescriptorSetLayout> m_BRDFLutSetLayout;
        Ref<VulkanDescriptorSetLayout> m_IrradianceSetLayout;
        Ref<VulkanDescriptorSetLayout> m_PrefilterSetLayout;
        Ref<VulkanDescriptorPool>      m_DescriptorPool;

        struct PipelineHandles
        {
            VkPipeline       Pipeline = VK_NULL_HANDLE;
            VkPipelineLayout Layout   = VK_NULL_HANDLE;
        };
        PipelineHandles m_BRDFLutPipeline;
        PipelineHandles m_IrradiancePipeline;
        PipelineHandles m_PrefilterPipeline;

        // 输出资源
        ImageHandle     m_BRDFLut;
        CubeImageHandle m_Irradiance; // mipLevels = 1
        CubeImageHandle m_Prefilter;  // 完整 mip 链（PREFILTER_MIP_LEVELS）

        VkImageView m_BRDFLutView    = VK_NULL_HANDLE; // 便利访问（= m_BRDFLut.View）
        VkImageView m_IrradianceView = VK_NULL_HANDLE;
        VkImageView m_PrefilterView  = VK_NULL_HANDLE;

        VkSampler m_LinearSampler = VK_NULL_HANDLE;

        bool m_IBLReady = false;

        static constexpr int IRRADIANCE_SIZE      = 32;
        static constexpr int PREFILTER_SIZE       = 128;
        static constexpr int PREFILTER_MIP_LEVELS = 5;
        static constexpr int BRDF_LUT_SIZE        = 512;
    };

} // namespace Engine
