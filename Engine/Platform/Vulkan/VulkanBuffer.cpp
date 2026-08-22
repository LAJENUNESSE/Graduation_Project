#include "engpch.h"
#include "Platform/Vulkan/VulkanBuffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/Vulkan/VulkanAllocator.h"
#include "Platform/Vulkan/VulkanContext.h"

#include <cstring>
#include <vma/vk_mem_alloc.h>

namespace Engine
{
    namespace
    {
        // host-visible 直写（host-visible 模式专用）
        void UploadToAllocation(VmaAllocation allocation, const void* data, uint32_t size, uint32_t offset = 0)
        {
            ENGINE_CORE_RELEASE_ASSERT(data != nullptr, "Upload source data must not be null");

            void*          mappedData = nullptr;
            const VkResult result     = vmaMapMemory(VulkanAllocator::GetAllocator(), allocation, &mappedData);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map VMA allocation");

            std::memcpy(static_cast<uint8_t*>(mappedData) + offset, data, size);
            vmaFlushAllocation(VulkanAllocator::GetAllocator(), allocation, offset, size);
            vmaUnmapMemory(VulkanAllocator::GetAllocator(), allocation);
        }

        // ------------------------------------------------------------------
        // 瞬态 staging 上传（device-local 模式专用）：staging(host) -> vkCmdCopyBuffer -> device。
        // 镜像 VulkanTexture2D::UploadPixels 模式；提交走 BeginSingleTimeCommands/
        // EndSingleTimeCommands（内部 vkQueueWaitIdle）。调用点均为低频路径
        // （初始化 / 基准 SetData），D-3 约束不受影响。
        // ------------------------------------------------------------------
        void UploadViaStaging(VkBuffer dstBuffer, const void* data, uint32_t size)
        {
            ENGINE_CORE_RELEASE_ASSERT(data != nullptr, "Upload source data must not be null");

            auto* context = VulkanContext::Get();
            ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required for staged upload");

            VkBuffer      stagingBuffer     = VK_NULL_HANDLE;
            VmaAllocation stagingAllocation = nullptr;

            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size        = size;
            stagingInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo stagingAlloc{};
            stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

            VkResult result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &stagingInfo, &stagingAlloc,
                                              &stagingBuffer, &stagingAllocation, nullptr);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer");

            void* mappedData = nullptr;
            result           = vmaMapMemory(VulkanAllocator::GetAllocator(), stagingAllocation, &mappedData);
            if (result == VK_SUCCESS)
            {
                std::memcpy(mappedData, data, size);
                vmaFlushAllocation(VulkanAllocator::GetAllocator(), stagingAllocation, 0, size);
                vmaUnmapMemory(VulkanAllocator::GetAllocator(), stagingAllocation);

                VkCommandBuffer cmd = context->BeginSingleTimeCommands();
                VkBufferCopy    region{};
                region.srcOffset = 0;
                region.dstOffset = 0;
                region.size      = size;
                vkCmdCopyBuffer(cmd, stagingBuffer, dstBuffer, 1, &region);
                context->EndSingleTimeCommands(cmd);

                vmaDestroyBuffer(VulkanAllocator::GetAllocator(), stagingBuffer, stagingAllocation);
            }
            else
            {
                vmaDestroyBuffer(VulkanAllocator::GetAllocator(), stagingBuffer, stagingAllocation);
                ENGINE_CORE_RELEASE_ASSERT(false, "Failed to map staging buffer");
            }
        }

        // ------------------------------------------------------------------
        // 同步回读（device-local 模式专用）：vkCmdCopyBuffer(device->staging) 后读出。
        // 仅调试可视化 / 基准 ReadBenchmarkParticles 等低频路径使用。
        // ------------------------------------------------------------------
        void DownloadViaStaging(VkBuffer srcBuffer, void* outData, uint32_t size)
        {
            ENGINE_CORE_RELEASE_ASSERT(outData != nullptr, "Readback output pointer must not be null");

            auto* context = VulkanContext::Get();
            ENGINE_CORE_RELEASE_ASSERT(context != nullptr, "VulkanContext required for staged readback");

            VkBuffer      stagingBuffer     = VK_NULL_HANDLE;
            VmaAllocation stagingAllocation = nullptr;

            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size        = size;
            stagingInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo stagingAlloc{};
            stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

            VkResult result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &stagingInfo, &stagingAlloc,
                                              &stagingBuffer, &stagingAllocation, nullptr);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create readback staging buffer");

            VkCommandBuffer cmd = context->BeginSingleTimeCommands();
            VkBufferCopy    region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size      = size;
            vkCmdCopyBuffer(cmd, srcBuffer, stagingBuffer, 1, &region);
            context->EndSingleTimeCommands(cmd); // QueueWaitIdle 保证拷贝完成

            void* mappedData = nullptr;
            result           = vmaMapMemory(VulkanAllocator::GetAllocator(), stagingAllocation, &mappedData);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map readback staging buffer");
            vmaInvalidateAllocation(VulkanAllocator::GetAllocator(), stagingAllocation, 0, size);
            std::memcpy(outData, mappedData, size);
            vmaUnmapMemory(VulkanAllocator::GetAllocator(), stagingAllocation);

            vmaDestroyBuffer(VulkanAllocator::GetAllocator(), stagingBuffer, stagingAllocation);
        }

        // ------------------------------------------------------------------
        // 分配模式：device-local 走 VMA AUTO_PREFER_DEVICE（GPUOnly/GPUDynamic
        // SSBO 与静态 VBO/IBO）；host-visible 保持 AUTO_PREFER_HOST + SEQUENTIAL_WRITE
        // （每帧 CPU 写入的 Counter/RigidBody/MeshSDF/UBO 及物理调试线框 VBO 依赖）。
        // ------------------------------------------------------------------
        struct AllocationMode
        {
            bool DeviceLocal = false;
        };

        void CreateBuffer(VkBuffer&          buffer,
                          VmaAllocation&     allocation,
                          uint32_t           size,
                          VkBufferUsageFlags usage,
                          const void*        initialData,
                          bool               deviceLocal)
        {
            ENGINE_CORE_RELEASE_ASSERT(size > 0, "Vulkan buffer size must be greater than zero");

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = size;
            bufferInfo.usage       = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (deviceLocal)
                bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT; // staging 上传目标

            VmaAllocationCreateInfo allocInfo{};
            if (deviceLocal)
            {
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            }
            else
            {
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            }

            const VkResult result = vmaCreateBuffer(VulkanAllocator::GetAllocator(), &bufferInfo, &allocInfo, &buffer,
                                                    &allocation, nullptr);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan buffer via VMA");

            if (initialData != nullptr)
            {
                if (deviceLocal)
                    UploadViaStaging(buffer, initialData, size);
                else
                    UploadToAllocation(allocation, initialData, size);
            }
        }
    } // namespace

    // ======================================================================
    // VulkanVertexBuffer
    // ======================================================================

    VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size)
    {
        // 动态 VBO（预分配 + SetData 流式更新，如物理调试线框）保持 host-visible；
        // 静态几何 VBO 走 device-local。构造时无法区分，统一 device-local，
        // PhysicsDebugDraw 的动态线框 VBO 由下方 SetData 的 device 分派兜底。
        // 注意：PhysicsDebugDraw 每帧 SetData 会走 staging 路径（QueueWaitIdle），
        // 仅调试场景可见，生产路径无影响。
        Create(size, nullptr, true);
    }

    VulkanVertexBuffer::VulkanVertexBuffer(float* vertices, uint32_t size)
    {
        Create(size, vertices, true);
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
        GpuMemoryStats::Get().AddUploaded(size);
        UploadViaStaging(m_Buffer, data, size);
    }

    void VulkanVertexBuffer::Create(uint32_t size, const void* initialData, bool deviceLocal)
    {
        m_Size = size;
        CreateBuffer(m_Buffer, m_Allocation, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, initialData, deviceLocal);
    }

    // ======================================================================
    // VulkanIndexBuffer
    // ======================================================================

    VulkanIndexBuffer::VulkanIndexBuffer(uint32_t* indices, uint32_t count) : m_Count(count)
    {
        m_Size = count * sizeof(uint32_t);
        CreateBuffer(m_Buffer, m_Allocation, m_Size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices, true);
    }

    VulkanIndexBuffer::~VulkanIndexBuffer()
    {
        if (m_Buffer != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyBuffer(VulkanAllocator::GetAllocator(), m_Buffer, m_Allocation);
    }

    void VulkanIndexBuffer::Bind() const {}

    void VulkanIndexBuffer::Unbind() const {}

    // ======================================================================
    // VulkanStorageBuffer
    // ======================================================================

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
        GpuMemoryStats::Get().AddUploaded(size);
        if (m_DeviceLocal)
            UploadViaStaging(m_Buffer, data, size);
        else
            UploadToAllocation(m_Allocation, data, size, offset);
    }

    void VulkanStorageBuffer::GetData(void* outData, uint32_t size, uint32_t offset) const
    {
        ENGINE_CORE_ASSERT(offset + size <= m_Size, "StorageBuffer::GetData out of bounds");
        ENGINE_CORE_ASSERT(outData != nullptr, "StorageBuffer::GetData output pointer must not be null");

        if (m_DeviceLocal)
        {
            // device-local：经 staging 同步回读（仅低频调试/基准路径）
            if (offset == 0 && size == m_Size)
            {
                DownloadViaStaging(m_Buffer, outData, size);
            }
            else
            {
                // 带偏移的局部回读：先整段拉回再切片（调用点均为小缓冲，可接受）
                std::vector<uint8_t> full(m_Size);
                DownloadViaStaging(m_Buffer, full.data(), m_Size);
                std::memcpy(outData, full.data() + offset, size);
            }
        }
        else
        {
            void*          mappedData = nullptr;
            const VkResult result     = vmaMapMemory(VulkanAllocator::GetAllocator(), m_Allocation, &mappedData);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map VMA allocation");

            vmaInvalidateAllocation(VulkanAllocator::GetAllocator(), m_Allocation, offset, size);
            std::memcpy(outData, static_cast<const uint8_t*>(mappedData) + offset, size);
            vmaUnmapMemory(VulkanAllocator::GetAllocator(), m_Allocation);
        }
    }

    void VulkanStorageBuffer::ClearToZero()
    {
        if (m_DeviceLocal)
        {
            // GPU 端 vkCmdFillBuffer 清零（防御性：当前 Vulkan 路径网格清零走
            // SpatialHashGrid::BuildVulkan 的 vkCmdFillBuffer，不触达此方法）
            auto*           context = VulkanContext::Get();
            VkCommandBuffer cmd     = context->BeginSingleTimeCommands();
            vkCmdFillBuffer(cmd, m_Buffer, 0, VK_WHOLE_SIZE, 0u);
            context->EndSingleTimeCommands(cmd);
        }
        else
        {
            void*          mappedData = nullptr;
            const VkResult result     = vmaMapMemory(VulkanAllocator::GetAllocator(), m_Allocation, &mappedData);
            ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to map VMA allocation");

            std::memset(mappedData, 0, m_Size);
            vmaFlushAllocation(VulkanAllocator::GetAllocator(), m_Allocation, 0, m_Size);
            vmaUnmapMemory(VulkanAllocator::GetAllocator(), m_Allocation);
        }
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

        m_Size        = size;
        m_Binding     = binding;
        m_GPUOnly     = gpuOnly;
        m_Dynamic     = dynamicStorage;
        m_DeviceLocal = gpuOnly || dynamicStorage; // GPUOnly/GPUDynamic 语义兑现：device-local 显存

        CreateBuffer(m_Buffer, m_Allocation, size,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                     initialData, m_DeviceLocal);

        m_LastBinding = binding;
    }

    // ======================================================================
    // VulkanUniformBuffer（每帧 CPU 写入，保持 host-visible）
    // ======================================================================

    VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding) : m_Size(size), m_Binding(binding)
    {
        CreateBuffer(m_Buffer, m_Allocation, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr, false);
    }

    VulkanUniformBuffer::~VulkanUniformBuffer()
    {
        if (m_Buffer != VK_NULL_HANDLE && VulkanAllocator::IsInitialized())
            vmaDestroyBuffer(VulkanAllocator::GetAllocator(), m_Buffer, m_Allocation);
    }

    void VulkanUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        ENGINE_CORE_ASSERT(offset + size <= m_Size, "UniformBuffer::SetData out of bounds");
        GpuMemoryStats::Get().AddUploaded(size);
        UploadToAllocation(m_Allocation, data, size, offset);
    }

} // namespace Engine
