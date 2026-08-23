#pragma once

#include "Platform/Vulkan/VulkanPipelineCache.h"
#include "Platform/Vulkan/VulkanShader.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Engine
{

    class VulkanDescriptorSetLayout;

    // 光栅状态位域（参与 pipeline cache key；与 GraphicsPipelineDesc 字段一一对应）
    namespace PipelineStateBits
    {
        constexpr uint32_t kDepthTest   = 1 << 0;
        constexpr uint32_t kDepthWrite  = 1 << 1;
        constexpr uint32_t kCullBack    = 1 << 2;
        constexpr uint32_t kCullFront   = 1 << 3;
        constexpr uint32_t kDepthLEqual = 1 << 4; // 0 = Less
    } // namespace PipelineStateBits

    // 场景 graphics pipeline 组装描述
    struct GraphicsPipelineDesc
    {
        VulkanShader* Shader               = nullptr;
        VkRenderPass  RenderPass           = VK_NULL_HANDLE;
        uint32_t      ColorAttachmentCount = 1;

        std::vector<VkVertexInputBindingDescription>   Bindings;
        std::vector<VkVertexInputAttributeDescription> Attributes;

        bool DepthTest   = true;
        bool DepthWrite  = true;
        bool DepthLEqual = false;
        bool CullBack    = true;
        bool CullFront   = false;
    };

    // Phase 8.2：从 VulkanShader 反射 + VulkanVertexArray 顶点描述组装 graphics pipeline，
    // 经 VulkanPipelineCache 按 key 缓存。per-shader 持有 descriptor set layouts 与
    // pipeline layout 所有权（由反射结果创建，shader 销毁前必须 Clear）。
    class VulkanGraphicsPipelineBuilder
    {
    public:
        VulkanPipelineCache::PipelineHandle GetOrCreate(VkDevice device, const GraphicsPipelineDesc& desc);

        // shader 析构时调用：释放该 shader 的 set layouts / pipeline layout，
        // 并清除缓存中属于它的 pipeline 条目（防止指针复用导致误命中）
        void ReleaseShader(VkDevice device, VulkanShader* shader);

        // 销毁全部 pipeline layout 与缓存 pipeline（device waitIdle 后由调用方保证）
        void Clear(VkDevice device);

    private:
        struct ShaderResources
        {
            std::vector<Ref<VulkanDescriptorSetLayout>> SetLayouts; // 按 set 下标
            VkPipelineLayout                            Layout = VK_NULL_HANDLE;
        };

        VkPipelineLayout GetOrCreatePipelineLayout(VkDevice device, VulkanShader* shader);
        static uint32_t  ComputeStateBits(const GraphicsPipelineDesc& desc);
        static uint64_t  ComputeVertexLayoutHash(const GraphicsPipelineDesc& desc);

        std::unordered_map<VulkanShader*, ShaderResources> m_ShaderResources;
        VulkanPipelineCache                                m_Cache;
    };

} // namespace Engine
