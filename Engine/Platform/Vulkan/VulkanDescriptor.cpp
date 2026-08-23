#include "engpch.h"
#include "Platform/Vulkan/VulkanDescriptor.h"

#include "Core/Assert.h"
#include "Core/Log.h"

namespace Engine
{

    // ===== VulkanDescriptorSetLayout =====

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VkDevice                                         device,
                                                         const std::vector<VkDescriptorSetLayoutBinding>& bindings)
        : m_Device(device), m_Bindings(bindings)
    {
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(m_Bindings.size());
        info.pBindings    = m_Bindings.empty() ? nullptr : m_Bindings.data();

        VkResult r = vkCreateDescriptorSetLayout(m_Device, &info, nullptr, &m_Layout);
        ENGINE_CORE_RELEASE_ASSERT(r == VK_SUCCESS, "Failed to create VkDescriptorSetLayout");
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
    {
        if (m_Layout != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
    }

    Ref<VulkanDescriptorSetLayout> VulkanDescriptorSetLayout::CreateFromReflection(
        VkDevice device, const std::vector<VulkanShader::ReflectedBinding>& reflected, uint32_t targetSet)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto& r : reflected)
        {
            if (r.Set != targetSet)
                continue;

            VkDescriptorSetLayoutBinding b{};
            b.binding            = r.Binding;
            b.descriptorType     = r.Type;
            b.descriptorCount    = r.Count;
            b.stageFlags         = r.Stages;
            b.pImmutableSamplers = nullptr;
            bindings.push_back(b);
        }

        return CreateRef<VulkanDescriptorSetLayout>(device, bindings);
    }

    // ===== VulkanDescriptorPool =====

    VulkanDescriptorPool::VulkanDescriptorPool(VkDevice                                 device,
                                               uint32_t                                 maxSets,
                                               const std::vector<VkDescriptorPoolSize>& poolSizes)
        : m_Device(device)
    {
        VkDescriptorPoolCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets       = maxSets;
        info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        info.pPoolSizes    = poolSizes.data();
        // 不设 FREE_DESCRIPTOR_SET_BIT；Reset 一次性回收全部 set，避免单 set free 开销
        info.flags = 0;

        VkResult r = vkCreateDescriptorPool(m_Device, &info, nullptr, &m_Pool);
        ENGINE_CORE_RELEASE_ASSERT(r == VK_SUCCESS, "Failed to create VkDescriptorPool");
    }

    VulkanDescriptorPool::~VulkanDescriptorPool()
    {
        if (m_Pool != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
    }

    VkDescriptorSet VulkanDescriptorPool::Allocate(VkDescriptorSetLayout layout)
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool     = m_Pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts        = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult        r   = vkAllocateDescriptorSets(m_Device, &info, &set);
        if (r != VK_SUCCESS)
        {
            ENGINE_CORE_WARN("[Vulkan] DescriptorPool allocate failed (code {}); pool may be exhausted",
                             static_cast<int>(r));
            return VK_NULL_HANDLE;
        }
        return set;
    }

    void VulkanDescriptorPool::Reset()
    {
        if (m_Pool != VK_NULL_HANDLE)
            vkResetDescriptorPool(m_Device, m_Pool, 0);
    }

    Ref<VulkanDescriptorPool> VulkanDescriptorPool::CreateDefaultComputePool(VkDevice device, uint32_t maxSetsPerType)
    {
        std::vector<VkDescriptorPoolSize> sizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSetsPerType},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSetsPerType * 4}, // SSBO 用量大
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSetsPerType},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSetsPerType},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSetsPerType},
            {VK_DESCRIPTOR_TYPE_SAMPLER, maxSetsPerType},
        };
        return CreateRef<VulkanDescriptorPool>(device, maxSetsPerType * 8, sizes);
    }

    // ===== VulkanDescriptorWriter =====

    VulkanDescriptorWriter& VulkanDescriptorWriter::WriteBuffer(
        uint32_t binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, VkDescriptorType type)
    {
        VkDescriptorBufferInfo info{};
        info.buffer = buffer;
        info.offset = offset;
        info.range  = range;
        m_BufferInfos.push_back(info);

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = type;
        m_Writes.push_back(w);
        m_Pending.push_back({true, static_cast<uint32_t>(m_BufferInfos.size() - 1)});
        return *this;
    }

    VulkanDescriptorWriter& VulkanDescriptorWriter::WriteImage(
        uint32_t binding, VkImageView view, VkImageLayout layout, VkDescriptorType type, VkSampler sampler)
    {
        VkDescriptorImageInfo info{};
        info.imageView   = view;
        info.imageLayout = layout;
        info.sampler     = sampler;
        m_ImageInfos.push_back(info);

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = type;
        m_Writes.push_back(w);
        m_Pending.push_back({false, static_cast<uint32_t>(m_ImageInfos.size() - 1)});
        return *this;
    }

    VulkanDescriptorWriter& VulkanDescriptorWriter::WriteImageElement(uint32_t         binding,
                                                                      uint32_t         elementIndex,
                                                                      VkImageView      view,
                                                                      VkImageLayout    layout,
                                                                      VkDescriptorType type,
                                                                      VkSampler        sampler)
    {
        VkDescriptorImageInfo info{};
        info.imageView   = view;
        info.imageLayout = layout;
        info.sampler     = sampler;
        m_ImageInfos.push_back(info);

        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstBinding      = binding;
        w.dstArrayElement = elementIndex;
        w.descriptorCount = 1;
        w.descriptorType  = type;
        m_Writes.push_back(w);
        m_Pending.push_back({false, static_cast<uint32_t>(m_ImageInfos.size() - 1)});
        return *this;
    }

    void VulkanDescriptorWriter::UpdateSet(VkDevice device, VkDescriptorSet set)
    {
        ENGINE_CORE_RELEASE_ASSERT(set != VK_NULL_HANDLE,
                                   "[Vulkan] DescriptorWriter::UpdateSet 收到 VK_NULL_HANDLE（pool 耗尽？）");

        for (size_t i = 0; i < m_Writes.size(); ++i)
        {
            m_Writes[i].dstSet = set;
            if (m_Pending[i].IsBuffer)
                m_Writes[i].pBufferInfo = &m_BufferInfos[m_Pending[i].InfoIndex];
            else
                m_Writes[i].pImageInfo = &m_ImageInfos[m_Pending[i].InfoIndex];
        }

        if (!m_Writes.empty())
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(m_Writes.size()), m_Writes.data(), 0, nullptr);

        m_Writes.clear();
        m_BufferInfos.clear();
        m_ImageInfos.clear();
        m_Pending.clear();
    }

} // namespace Engine
