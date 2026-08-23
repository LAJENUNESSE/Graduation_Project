#include "engpch.h"
#include "Platform/Vulkan/VulkanPipelineCache.h"

#include "Core/Assert.h"
#include "Core/Log.h"

namespace Engine
{

    VulkanPipelineCache::~VulkanPipelineCache()
    {
        // 析构时 device 通常未知；由 Clear(VkDevice) 显式销毁。
        // 若仍有残留，仅释放 map 内存（pipeline 泄漏由调用方生命周期保证避免）。
        m_Pipelines.clear();
    }

    VulkanPipelineCache::PipelineHandle VulkanPipelineCache::GetOrCreate(const VulkanGraphicsPipelineKey&       key,
                                                                         const std::function<PipelineHandle()>& buildFn)
    {
        auto it = m_Pipelines.find(key);
        if (it != m_Pipelines.end())
            return it->second;

        ENGINE_CORE_RELEASE_ASSERT(buildFn, "VulkanPipelineCache buildFn must be valid");

        PipelineHandle handle = buildFn();
        if (handle.Pipeline != VK_NULL_HANDLE)
            m_Pipelines[key] = handle;

        return handle;
    }

    bool VulkanPipelineCache::Contains(const VulkanGraphicsPipelineKey& key) const
    {
        return m_Pipelines.find(key) != m_Pipelines.end();
    }

    // 只销毁 Pipeline。handle.Layout 由持有方（VulkanGraphicsPipelineBuilder 的
    // per-shader 资源）负责销毁，此处不触碰以避免双重释放。
    void VulkanPipelineCache::Clear(VkDevice device)
    {
        if (device != VK_NULL_HANDLE)
        {
            for (auto& [_, handle] : m_Pipelines)
            {
                if (handle.Pipeline != VK_NULL_HANDLE)
                    vkDestroyPipeline(device, handle.Pipeline, nullptr);
            }
        }
        m_Pipelines.clear();
    }

} // namespace Engine
