#pragma once

#include <functional>
#include <unordered_map>

#include <vulkan/vulkan.h>

namespace Engine
{

    // graphics pipeline 缓存 key：shader + render pass + 顶点布局指纹。
    // 本阶段（Phase 8.2 地基）只提供存储/查找/销毁骨架，未被主帧渲染接入。
    struct VulkanGraphicsPipelineKey
    {
        const void*  Shader           = nullptr; // VulkanShader*（标识，不托管）
        VkRenderPass RenderPass       = VK_NULL_HANDLE;
        uint64_t     VertexLayoutHash = 0;

        bool operator==(const VulkanGraphicsPipelineKey& other) const
        {
            return Shader == other.Shader && RenderPass == other.RenderPass &&
                   VertexLayoutHash == other.VertexLayoutHash;
        }
    };

    struct VulkanGraphicsPipelineKeyHash
    {
        size_t operator()(const VulkanGraphicsPipelineKey& key) const noexcept
        {
            size_t h = std::hash<const void*>{}(key.Shader);
            h ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(key.RenderPass)) + 0x9e3779b97f4a7c15ULL + (h << 6) +
                 (h >> 2);
            h ^= std::hash<uint64_t>{}(key.VertexLayoutHash) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    // graphics pipeline 懒创建缓存。每个 entry 持有 pipeline + layout 所有权。
    class VulkanPipelineCache
    {
    public:
        VulkanPipelineCache() = default;
        ~VulkanPipelineCache();

        VulkanPipelineCache(const VulkanPipelineCache&)            = delete;
        VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;

        struct PipelineHandle
        {
            VkPipeline       Pipeline = VK_NULL_HANDLE;
            VkPipelineLayout Layout   = VK_NULL_HANDLE;
        };

        // 命中返回缓存 handle；未命中调用 buildFn 创建并缓存。
        // buildFn 返回的 handle 由本 cache 接管所有权（析构时统一销毁）。
        PipelineHandle GetOrCreate(const VulkanGraphicsPipelineKey&       key,
                                   const std::function<PipelineHandle()>& buildFn);

        // 销毁全部缓存（device 为空则仅清空 map，不调用 vkDestroyXXX，防御静态析构顺序）
        void Clear(VkDevice device);

        // 是否已命中
        bool Contains(const VulkanGraphicsPipelineKey& key) const;

    private:
        std::unordered_map<VulkanGraphicsPipelineKey, PipelineHandle, VulkanGraphicsPipelineKeyHash> m_Pipelines;
    };

} // namespace Engine
