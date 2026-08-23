#pragma once

#include "Platform/Vulkan/VulkanShader.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Engine
{

    // ---- Descriptor Set Layout ----
    // 持有 VkDescriptorSetLayout 所有权；从 VulkanShader 反射结果创建
    class VulkanDescriptorSetLayout
    {
    public:
        VulkanDescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
        ~VulkanDescriptorSetLayout();

        VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&)            = delete;
        VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;

        VkDescriptorSetLayout GetHandle() const { return m_Layout; }

        const std::vector<VkDescriptorSetLayoutBinding>& GetBindings() const { return m_Bindings; }

        // 从 VulkanShader 反射结果筛选指定 set 的 binding 并创建 layout
        static Ref<VulkanDescriptorSetLayout> CreateFromReflection(
            VkDevice device, const std::vector<VulkanShader::ReflectedBinding>& reflected, uint32_t targetSet = 0);

    private:
        VkDevice                                  m_Device = VK_NULL_HANDLE;
        VkDescriptorSetLayout                     m_Layout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSetLayoutBinding> m_Bindings;
    };

    // ---- Descriptor Pool ----
    // 持有 VkDescriptorPool；支持 Allocate(layout) + Reset()（per-frame 复用模式）
    class VulkanDescriptorPool
    {
    public:
        VulkanDescriptorPool(VkDevice device, uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& poolSizes);
        ~VulkanDescriptorPool();

        VulkanDescriptorPool(const VulkanDescriptorPool&)            = delete;
        VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;

        // 从 pool 分配一个 descriptor set；失败返回 VK_NULL_HANDLE（pool 已满）
        VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

        // 重置整个 pool，所有分配的 set 失效（per-frame 复用）
        void Reset();

        VkDescriptorPool GetHandle() const { return m_Pool; }

        // 便利工厂：创建一个适合通用 compute 用途的 pool（UBO+SSBO+SampledImage+StorageImage 各 N 个，maxSets=N）
        static Ref<VulkanDescriptorPool> CreateDefaultComputePool(VkDevice device, uint32_t maxSetsPerType = 256);

    private:
        VkDevice         m_Device = VK_NULL_HANDLE;
        VkDescriptorPool m_Pool   = VK_NULL_HANDLE;
    };

    // ---- Descriptor Writer ----
    // Builder 风格累积 VkWriteDescriptorSet，最后 UpdateSet 一次性提交
    class VulkanDescriptorWriter
    {
    public:
        VulkanDescriptorWriter&
        WriteBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, VkDescriptorType type);

        VulkanDescriptorWriter& WriteImage(uint32_t         binding,
                                           VkImageView      view,
                                           VkImageLayout    layout,
                                           VkDescriptorType type,
                                           VkSampler        sampler = VK_NULL_HANDLE);

        // 数组 binding 的单元素写入（dstArrayElement）；CSM sampler 数组等使用
        VulkanDescriptorWriter& WriteImageElement(uint32_t         binding,
                                                  uint32_t         elementIndex,
                                                  VkImageView      view,
                                                  VkImageLayout    layout,
                                                  VkDescriptorType type,
                                                  VkSampler        sampler = VK_NULL_HANDLE);

        // 把累积的 write 提交到指定 set；调用后 writer 内部状态被清空
        void UpdateSet(VkDevice device, VkDescriptorSet set);

    private:
        // 单独保存 VkDescriptorBufferInfo / VkDescriptorImageInfo，避免 pWriteDescriptorSet 指针失效
        std::vector<VkDescriptorBufferInfo> m_BufferInfos;
        std::vector<VkDescriptorImageInfo>  m_ImageInfos;
        std::vector<VkWriteDescriptorSet>   m_Writes;
        // 记录每个 write 的 info 类型与索引，UpdateSet 时回填指针
        struct Pending
        {
            bool     IsBuffer;
            uint32_t InfoIndex;
        };
        std::vector<Pending> m_Pending;
    };

} // namespace Engine
