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
    // - 输出 image 在 compute dispatch 期间布局为 GENERAL，之后转为 SHADER_READ_ONLY_OPTIMAL 供 PBR pass 采样
    //
    // 注：当前 VulkanTextureCubemap 尚未完成（Phase 7.x 后续步骤），所以 Generate() 在 skybox 数据无效时
    // 只完成 BRDF LUT，跳过 Irradiance/Prefilter（保留接口契约不崩）。
    class VulkanIBLGenerator : public IBLGenerator
    {
    public:
        VulkanIBLGenerator()           = default;
        ~VulkanIBLGenerator() override = default;

        bool Init() override;
        void Generate(const Ref<TextureCubemap>& skybox) override;
        void Clear() override;
        void Shutdown() override;

        // GetXxxID 在 Vulkan 路径下不再是 OpenGL texture ID。
        // 当前 PBR 采样链路在 Vulkan 路径暂未接入（EditorApp 走 VulkanSmokeLayer），返回 0 占位。
        // 后续 Vulkan PBR pass 接入时，应改为返回 VkImageView 句柄的 cast 或换接口。
        uint32_t GetIrradianceMapID() const override { return 0; }
        uint32_t GetPrefilterMapID() const override { return 0; }
        uint32_t GetBRDFLutID() const override { return 0; }
        bool     IsReady() const override { return m_IBLReady; }

        // 可选访问器（供未来 Vulkan PBR pass 使用）
        VkImageView GetBRDFLutView() const { return m_BRDFLutView; }
        VkImageView GetIrradianceView() const { return m_IrradianceView; }
        VkImageView GetPrefilterView() const { return m_PrefilterView; }

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
        ImageHandle m_BRDFLut;
        ImageHandle m_Irradiance; // 6 面 atlas 形式（与 OpenGL 路径一致：横排 6 面）
        ImageHandle m_Prefilter;  // 仅 mip0（mip 链由后续 Vulkan PBR 接入时再补）

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
