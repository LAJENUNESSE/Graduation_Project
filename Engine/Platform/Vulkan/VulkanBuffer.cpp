#include "engpch.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"

#include <cstring>

namespace Engine
{

    // ============================================================================
    // VulkanBufferUtils
    // ============================================================================

    void VulkanBufferUtils::CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                                         VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                                         VkDeviceMemory& bufferMemory)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = size;
        bufferInfo.usage       = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan buffer!");

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

        result = vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory);
        ENGINE_CORE_RELEASE_ASSERT(result == VK_SUCCESS, "Failed to allocate Vulkan buffer memory!");

        vkBindBufferMemory(device, buffer, bufferMemory, 0);
    }

    void VulkanBufferUtils::CopyToBuffer(VkDevice device, VkDeviceMemory memory, const void* data, VkDeviceSize size,
                                         VkDeviceSize offset)
    {
        void* mapped;
        vkMapMemory(device, memory, offset, size, 0, &mapped);
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(device, memory);
    }

    void VulkanBufferUtils::CopyFromBuffer(VkDevice device, VkDeviceMemory memory, void* data, VkDeviceSize size,
                                           VkDeviceSize offset)
    {
        void* mapped;
        vkMapMemory(device, memory, offset, size, 0, &mapped);
        std::memcpy(data, mapped, static_cast<size_t>(size));
        vkUnmapMemory(device, memory);
    }

    void VulkanBufferUtils::CopyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer srcBuffer,
                                       VkBuffer dstBuffer, VkDeviceSize size)
    {
        // Single-time command buffer for copy operation
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &commandBuffer;

        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    uint32_t VulkanBufferUtils::FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                               VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Failed to find suitable memory type!");
        return 0;
    }

    // ============================================================================
    // VulkanVertexBuffer
    // ============================================================================

    VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size) : m_Size(size)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        // Create host-visible buffer for dynamic data
        VulkanBufferUtils::CreateBuffer(context->GetDevice(), context->GetPhysicalDevice(), size,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        m_Buffer, m_BufferMemory);
    }

    VulkanVertexBuffer::VulkanVertexBuffer(float* vertices, uint32_t size) : m_Size(size)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        // For static data, use staging buffer for optimal performance
        // TODO: Use device-local memory with staging buffer for production
        // For now, use host-visible memory for simplicity
        VulkanBufferUtils::CreateBuffer(context->GetDevice(), context->GetPhysicalDevice(), size,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        m_Buffer, m_BufferMemory);

        VulkanBufferUtils::CopyToBuffer(context->GetDevice(), m_BufferMemory, vertices, size);
    }

    VulkanVertexBuffer::~VulkanVertexBuffer()
    {
        auto* context = VulkanContext::Get();
        if (context && context->GetDevice() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(context->GetDevice());
            if (m_Buffer != VK_NULL_HANDLE)
                vkDestroyBuffer(context->GetDevice(), m_Buffer, nullptr);
            if (m_BufferMemory != VK_NULL_HANDLE)
                vkFreeMemory(context->GetDevice(), m_BufferMemory, nullptr);
        }
    }

    void VulkanVertexBuffer::Bind() const
    {
        // In Vulkan, binding happens during command buffer recording
        // This is a no-op for now, actual binding done in VulkanRendererAPI
    }

    void VulkanVertexBuffer::Unbind() const
    {
        // No-op in Vulkan
    }

    void VulkanVertexBuffer::SetData(const void* data, uint32_t size)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");
        ENGINE_CORE_RELEASE_ASSERT(size <= m_Size, "Data size exceeds buffer size!");

        VulkanBufferUtils::CopyToBuffer(context->GetDevice(), m_BufferMemory, data, size);
    }

    // ============================================================================
    // VulkanIndexBuffer
    // ============================================================================

    VulkanIndexBuffer::VulkanIndexBuffer(uint32_t* indices, uint32_t count) : m_Count(count)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        VkDeviceSize bufferSize = count * sizeof(uint32_t);

        // TODO: Use device-local memory with staging buffer for production
        VulkanBufferUtils::CreateBuffer(context->GetDevice(), context->GetPhysicalDevice(), bufferSize,
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        m_Buffer, m_BufferMemory);

        VulkanBufferUtils::CopyToBuffer(context->GetDevice(), m_BufferMemory, indices, bufferSize);
    }

    VulkanIndexBuffer::~VulkanIndexBuffer()
    {
        auto* context = VulkanContext::Get();
        if (context && context->GetDevice() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(context->GetDevice());
            if (m_Buffer != VK_NULL_HANDLE)
                vkDestroyBuffer(context->GetDevice(), m_Buffer, nullptr);
            if (m_BufferMemory != VK_NULL_HANDLE)
                vkFreeMemory(context->GetDevice(), m_BufferMemory, nullptr);
        }
    }

    void VulkanIndexBuffer::Bind() const
    {
        // In Vulkan, binding happens during command buffer recording
    }

    void VulkanIndexBuffer::Unbind() const
    {
        // No-op in Vulkan
    }

    // ============================================================================
    // VulkanUniformBuffer
    // ============================================================================

    VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding) : m_Size(size), m_Binding(binding)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        // Uniform buffers are frequently updated, use host-visible memory with persistent mapping
        VulkanBufferUtils::CreateBuffer(context->GetDevice(), context->GetPhysicalDevice(), size,
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        m_Buffer, m_BufferMemory);

        // Persistent mapping for frequent updates
        vkMapMemory(context->GetDevice(), m_BufferMemory, 0, size, 0, &m_MappedMemory);
    }

    VulkanUniformBuffer::~VulkanUniformBuffer()
    {
        auto* context = VulkanContext::Get();
        if (context && context->GetDevice() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(context->GetDevice());
            if (m_MappedMemory)
                vkUnmapMemory(context->GetDevice(), m_BufferMemory);
            if (m_Buffer != VK_NULL_HANDLE)
                vkDestroyBuffer(context->GetDevice(), m_Buffer, nullptr);
            if (m_BufferMemory != VK_NULL_HANDLE)
                vkFreeMemory(context->GetDevice(), m_BufferMemory, nullptr);
        }
    }

    void VulkanUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        ENGINE_CORE_RELEASE_ASSERT(m_MappedMemory, "Uniform buffer not mapped!");
        ENGINE_CORE_RELEASE_ASSERT(offset + size <= m_Size, "Data exceeds buffer bounds!");

        std::memcpy(static_cast<char*>(m_MappedMemory) + offset, data, size);
    }

    VkDescriptorBufferInfo VulkanUniformBuffer::GetDescriptorInfo() const
    {
        VkDescriptorBufferInfo info{};
        info.buffer = m_Buffer;
        info.offset = 0;
        info.range  = m_Size;
        return info;
    }

    // ============================================================================
    // VulkanStorageBuffer
    // ============================================================================

    VulkanStorageBuffer::VulkanStorageBuffer(uint32_t size, uint32_t binding) : m_Size(size), m_Binding(binding)
    {
        CreateHostVisibleBuffer(size, nullptr);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding)
        : m_Size(size), m_Binding(binding)
    {
        CreateHostVisibleBuffer(size, data);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(uint32_t size, uint32_t binding, bool gpuOnly)
        : m_Size(size), m_Binding(binding), m_DeviceLocal(gpuOnly)
    {
        if (gpuOnly)
            CreateDeviceLocalBuffer(size, nullptr, false);
        else
            CreateHostVisibleBuffer(size, nullptr);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, bool gpuOnly)
        : m_Size(size), m_Binding(binding), m_DeviceLocal(gpuOnly)
    {
        if (gpuOnly)
            CreateDeviceLocalBuffer(size, data, false);
        else
            CreateHostVisibleBuffer(size, data);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(uint32_t size, uint32_t binding, DynamicStorageTag)
        : m_Size(size), m_Binding(binding), m_DeviceLocal(true), m_HasStaging(true)
    {
        CreateDeviceLocalBuffer(size, nullptr, true);
    }

    VulkanStorageBuffer::VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, DynamicStorageTag)
        : m_Size(size), m_Binding(binding), m_DeviceLocal(true), m_HasStaging(true)
    {
        CreateDeviceLocalBuffer(size, data, true);
    }

    VulkanStorageBuffer::~VulkanStorageBuffer()
    {
        auto* context = VulkanContext::Get();
        if (context && context->GetDevice() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(context->GetDevice());

            if (m_MappedMemory && !m_DeviceLocal)
                vkUnmapMemory(context->GetDevice(), m_BufferMemory);
            if (m_MappedMemory && m_HasStaging)
                vkUnmapMemory(context->GetDevice(), m_StagingMemory);

            if (m_Buffer != VK_NULL_HANDLE)
                vkDestroyBuffer(context->GetDevice(), m_Buffer, nullptr);
            if (m_BufferMemory != VK_NULL_HANDLE)
                vkFreeMemory(context->GetDevice(), m_BufferMemory, nullptr);
            if (m_StagingBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(context->GetDevice(), m_StagingBuffer, nullptr);
            if (m_StagingMemory != VK_NULL_HANDLE)
                vkFreeMemory(context->GetDevice(), m_StagingMemory, nullptr);
        }
    }

    void VulkanStorageBuffer::Bind(uint32_t binding) const
    {
        // In Vulkan, binding happens through descriptor sets
        // This is tracked for compatibility but actual binding done elsewhere
        (void)binding;
    }

    void VulkanStorageBuffer::Unbind() const
    {
        // No-op in Vulkan
    }

    void VulkanStorageBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
    {
        ENGINE_CORE_RELEASE_ASSERT(offset + size <= m_Size, "StorageBuffer::SetData out of bounds");

        if (m_DeviceLocal && m_HasStaging)
        {
            // Copy to staging buffer, then transfer to device-local
            ENGINE_CORE_RELEASE_ASSERT(m_MappedMemory, "Staging buffer not mapped!");
            std::memcpy(static_cast<char*>(m_MappedMemory) + offset, data, size);

            // TODO: Queue copy command (requires command pool access)
            // For now this is a placeholder - actual transfer needs command buffer
            ENGINE_CORE_WARN("VulkanStorageBuffer::SetData with staging - transfer not yet implemented");
        }
        else if (!m_DeviceLocal)
        {
            ENGINE_CORE_RELEASE_ASSERT(m_MappedMemory, "Buffer not mapped!");
            std::memcpy(static_cast<char*>(m_MappedMemory) + offset, data, size);
        }
        else
        {
            ENGINE_CORE_RELEASE_ASSERT(false, "Cannot SetData on GPU-only buffer without staging!");
        }
    }

    void VulkanStorageBuffer::GetData(void* data, uint32_t size, uint32_t offset) const
    {
        ENGINE_CORE_RELEASE_ASSERT(offset + size <= m_Size, "StorageBuffer::GetData out of bounds");

        if (!m_DeviceLocal)
        {
            ENGINE_CORE_RELEASE_ASSERT(m_MappedMemory, "Buffer not mapped!");
            std::memcpy(data, static_cast<const char*>(m_MappedMemory) + offset, size);
        }
        else
        {
            // TODO: Need to copy from device-local to staging, then read
            ENGINE_CORE_RELEASE_ASSERT(false, "GetData from device-local buffer not yet implemented!");
        }
    }

    VkDescriptorBufferInfo VulkanStorageBuffer::GetDescriptorInfo() const
    {
        VkDescriptorBufferInfo info{};
        info.buffer = m_Buffer;
        info.offset = 0;
        info.range  = m_Size;
        return info;
    }

    void VulkanStorageBuffer::CreateHostVisibleBuffer(uint32_t size, const void* data)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        VulkanBufferUtils::CreateBuffer(context->GetDevice(), context->GetPhysicalDevice(), size,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        m_Buffer, m_BufferMemory);

        // Persistent mapping
        vkMapMemory(context->GetDevice(), m_BufferMemory, 0, size, 0, &m_MappedMemory);

        if (data)
            std::memcpy(m_MappedMemory, data, size);
    }

    void VulkanStorageBuffer::CreateDeviceLocalBuffer(uint32_t size, const void* data, bool withStaging)
    {
        auto* context = VulkanContext::Get();
        ENGINE_CORE_RELEASE_ASSERT(context, "VulkanContext not initialized!");

        // Create device-local buffer
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (withStaging || data)
            usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VulkanBufferUtils::CreateBuffer(context->GetDevice(), context->GetPhysicalDevice(), size, usage,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Buffer, m_BufferMemory);

        if (withStaging)
        {
            // Create persistent staging buffer
            VulkanBufferUtils::CreateBuffer(
                context->GetDevice(), context->GetPhysicalDevice(), size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_StagingBuffer,
                m_StagingMemory);

            vkMapMemory(context->GetDevice(), m_StagingMemory, 0, size, 0, &m_MappedMemory);

            if (data)
            {
                std::memcpy(m_MappedMemory, data, size);
                // TODO: Queue initial transfer
            }
        }
        else if (data)
        {
            // One-time staging buffer for initial data
            VkBuffer       stagingBuffer;
            VkDeviceMemory stagingMemory;

            VulkanBufferUtils::CreateBuffer(
                context->GetDevice(), context->GetPhysicalDevice(), size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                stagingMemory);

            VulkanBufferUtils::CopyToBuffer(context->GetDevice(), stagingMemory, data, size);

            // TODO: Copy staging -> device-local using command buffer
            // For now, cleanup staging immediately (data transfer incomplete)
            ENGINE_CORE_WARN("VulkanStorageBuffer: Initial data transfer to device-local not yet implemented");

            vkDestroyBuffer(context->GetDevice(), stagingBuffer, nullptr);
            vkFreeMemory(context->GetDevice(), stagingMemory, nullptr);
        }
    }

} // namespace Engine
