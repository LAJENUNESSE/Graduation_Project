#include "engpch.h"
#include "Platform/Vulkan/VulkanBuffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanAllocator.h"

#include <algorithm>
#include <cstring>
#include <vma/vk_mem_alloc.h>

namespace Engine
{
    namespace
    {
        void UploadToAllocation(VmaAllocation allocation, const void* data, uint32_t size, uint32_t offset = 0)
        {
            ENGINE_CORE_RELEASE_ASSERT(data != nullptr, "Upload source data must not be null");

            void*          mappedData = nullptr;
            const VkResult result     = vmaMapMemory(VulkanAllocator::GetAllocator(), allocation, &mappedData);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map VMA allocation");

            std::memcpy(static_cast<uint8_t*>(mappedData) + offset, data, size);
            vmaUnmapMemory(VulkanAllocator::GetAllocator(), allocation);
        }

        void CreateBuffer(VkBuffer&          buffer,
                          VmaAllocation&     allocation,
                          uint32_t           size,
                          VkBufferUsageFlags usage,
                          const void*        initialData)
        {
            ENGINE_CORE_RELEASE_ASSERT(size > 0, "Vulkan buffer size must be greater than zero");

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = size;
            bufferInfo.usage       = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

            const VkResult result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo, &buffer,
                                                    &allocation, nullptr);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan buffer via VMA");

            if (initialData != nullptr)
                UploadToAllocation(allocation, initialData, size);
        }
    } // namespace

    VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size)
    {
        Create(size, nullptr);
    }

    VulkanVertexBuffer::VulkanVertexBuffer(float* vertices, uint32_t size)
    {
        Create(size, vertices);
    }

    VulkanVertexBuffer::~VulkanVertexBuffer()
    {
        if (m_Buffer != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyBuffer(VulkanAllocator::GetAllocator(), m_Buffer, m_Allocation);
    }

    void VulkanVertexBuffer::Bind() const {}

    void VulkanVertexBuffer::Unbind() const {}

    void VulkanVertexBuffer::SetData(const void* data, uint32_t size)
    {
        ENGINE_CORE_RELEASE_ASSERT(size <= m_Size, "Vertex buffer SetData exceeds allocation size");
        UploadToAllocation(m_Allocation, data, size);
    }

    void VulkanVertexBuffer::Create(uint32_t size, const void* initialData)
    {
        m_Size = size;
        CreateBuffer(m_Buffer, m_Allocation, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, initialData);
    }

    VulkanIndexBuffer::VulkanIndexBuffer(uint32_t* indices, uint32_t count) : m_Count(count)
    {
        m_Size = count * sizeof(uint32_t);
        CreateBuffer(m_Buffer, m_Allocation, m_Size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices);
    }

    VulkanIndexBuffer::~VulkanIndexBuffer()
    {
        if (m_Buffer != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyBuffer(VulkanAllocator::GetAllocator(), m_Buffer, m_Allocation);
    }

    void VulkanIndexBuffer::Bind() const {}

    void VulkanIndexBuffer::Unbind() const {}

    VulkanStorageBuffer::VulkanStorageBuffer(uint32_t size, uint32_t binding, ExternalMemoryHint hint)
    {
        Create(size, binding, nullptr, false, false, hint);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, ExternalMemoryHint hint)
    {
        Create(size, binding, data, false, false, hint);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(uint32_t size, uint32_t binding, bool gpuOnly, ExternalMemoryHint hint)
    {
        Create(size, binding, nullptr, gpuOnly, false, hint);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(
        const void* data, uint32_t size, uint32_t binding, bool gpuOnly, ExternalMemoryHint hint)
    {
        Create(size, binding, data, gpuOnly, false, hint);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(uint32_t size, uint32_t binding, DynamicStorageTag)
    {
        Create(size, binding, nullptr, false, true, ExternalMemoryHint::None);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, DynamicStorageTag)
    {
        Create(size, binding, data, false, true, ExternalMemoryHint::None);
    }

    VulkanStorageBuffer::~VulkanStorageBuffer()
    {
        if (m_Buffer != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyBuffer(VulkanAllocator::GetAllocator(), m_Buffer, m_Allocation);
    }

    void VulkanStorageBuffer::Bind(uint32_t binding) const
    {
        m_LastBinding = binding;
    }

    void VulkanStorageBuffer::Unbind() const
    {
        m_LastBinding = m_Binding;
    }

    void VulkanStorageBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        ENGINE_CORE_ASSERT(offset + size <= m_Size, "StorageBuffer::SetData out of bounds");
        UploadToAllocation(m_Allocation, data, size, offset);
    }

    void VulkanStorageBuffer::GetData(void* outData, uint32_t size, uint32_t offset) const
    {
        ENGINE_CORE_ASSERT(offset + size <= m_Size, "StorageBuffer::GetData out of bounds");
        ENGINE_CORE_ASSERT(outData != nullptr, "StorageBuffer::GetData output pointer must not be null");

        void*          mappedData = nullptr;
        const VkResult result     = vmaMapMemory(VulkanAllocator::GetAllocator(), m_Allocation, &mappedData);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map VMA allocation");

        std::memcpy(outData, static_cast<const uint8_t*>(mappedData) + offset, size);
        vmaUnmapMemory(VulkanAllocator::GetAllocator(), m_Allocation);
    }

    void VulkanStorageBuffer::ClearToZero()
    {
        void*          mappedData = nullptr;
        const VkResult result     = vmaMapMemory(VulkanAllocator::GetAllocator(), m_Allocation, &mappedData);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map VMA allocation");

        std::memset(mappedData, 0, m_Size);
        vmaUnmapMemory(VulkanAllocator::GetAllocator(), m_Allocation);
    }

    void VulkanStorageBuffer::Create(uint32_t           size,
                                     uint32_t           binding,
                                     const void*        initialData,
                                     bool               gpuOnly,
                                     bool               dynamicStorage,
                                     ExternalMemoryHint hint)
    {
        // Phase 7.5 占位：CUDA-Vulkan 互操作路径需要 VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
        // 创建可导出的 buffer + VkExportMemoryAllocateInfo，搭配 cuImportExternalMemory 实现零拷贝。
        // Phase 7 仅暴露枚举接口让上层先标注用途，实现留到 Phase 7.5。
        ENGINE_CORE_RELEASE_ASSERT(hint == ExternalMemoryHint::None,
                                   "ExternalMemoryHint::CudaInterop is reserved for Phase 7.5; not yet implemented");

        m_Size    = size;
        m_Binding = binding;
        m_GPUOnly = gpuOnly;
        m_Dynamic = dynamicStorage;

        CreateBuffer(m_Buffer, m_Allocation, size,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                     initialData);

        m_LastBinding = binding;

        static bool warnedStorageModes = false;
        if ((gpuOnly || dynamicStorage) && !warnedStorageModes)
        {
            warnedStorageModes = true;
            ENGINE_CORE_WARN("[Vulkan] VulkanStorageBuffer currently treats gpuOnly/dynamic modes as the same "
                             "host-visible VMA path");
        }
    }

    VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding) : m_Size(size), m_Binding(binding)
    {
        CreateBuffer(m_Buffer, m_Allocation, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr);
    }

    VulkanUniformBuffer::~VulkanUniformBuffer()
    {
        if (m_Buffer != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyBuffer(VulkanAllocator::GetAllocator(), m_Buffer, m_Allocation);
    }

    void VulkanUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        ENGINE_CORE_ASSERT(offset + size <= m_Size, "UniformBuffer::SetData out of bounds");
        UploadToAllocation(m_Allocation, data, size, offset);
    }

} // namespace Engine
