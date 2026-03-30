#pragma once

#include "Renderer/Buffer.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/StorageBuffer.h"

#include <vulkan/vulkan.h>

namespace Engine
{
    class VulkanContext;

    // ============================================================================
    // VulkanBufferUtils - Static helpers for buffer creation/memory allocation
    // ============================================================================
    class VulkanBufferUtils
    {
    public:
        // Create a VkBuffer with associated device memory
        static void CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                                 VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                                 VkDeviceMemory& bufferMemory);

        // Copy data to device memory (for host-visible memory)
        static void CopyToBuffer(VkDevice device, VkDeviceMemory memory, const void* data, VkDeviceSize size,
                                 VkDeviceSize offset = 0);

        // Copy data from device memory (for host-visible memory)
        static void CopyFromBuffer(VkDevice device, VkDeviceMemory memory, void* data, VkDeviceSize size,
                                   VkDeviceSize offset = 0);

        // Copy buffer to buffer using command buffer (for device-local memory)
        static void CopyBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkBuffer srcBuffer,
                               VkBuffer dstBuffer, VkDeviceSize size);

        // Find suitable memory type index
        static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                       VkMemoryPropertyFlags properties);
    };

    // ============================================================================
    // VulkanVertexBuffer
    // ============================================================================
    class VulkanVertexBuffer : public VertexBuffer
    {
    public:
        VulkanVertexBuffer(uint32_t size);
        VulkanVertexBuffer(float* vertices, uint32_t size);
        ~VulkanVertexBuffer() override;

        void Bind() const override;
        void Unbind() const override;

        void SetData(const void* data, uint32_t size) override;

        const BufferLayout& GetLayout() const override { return m_Layout; }
        void                SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        VkBuffer       m_Buffer       = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        uint32_t       m_Size         = 0;
        BufferLayout   m_Layout;
    };

    // ============================================================================
    // VulkanIndexBuffer
    // ============================================================================
    class VulkanIndexBuffer : public IndexBuffer
    {
    public:
        VulkanIndexBuffer(uint32_t* indices, uint32_t count);
        ~VulkanIndexBuffer() override;

        void Bind() const override;
        void Unbind() const override;

        uint32_t GetCount() const override { return m_Count; }

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        VkBuffer       m_Buffer       = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        uint32_t       m_Count        = 0;
    };

    // ============================================================================
    // VulkanUniformBuffer
    // ============================================================================
    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer(uint32_t size, uint32_t binding);
        ~VulkanUniformBuffer() override;

        void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

        VkBuffer             GetBuffer() const { return m_Buffer; }
        VkDescriptorBufferInfo GetDescriptorInfo() const;

    private:
        VkBuffer       m_Buffer       = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory = VK_NULL_HANDLE;
        void*          m_MappedMemory = nullptr; // Persistently mapped for frequent updates
        uint32_t       m_Size         = 0;
        uint32_t       m_Binding      = 0;
    };

    // ============================================================================
    // VulkanStorageBuffer
    // ============================================================================
    class VulkanStorageBuffer : public ShaderStorageBuffer
    {
    public:
        // Tag types matching OpenGL implementation
        struct DynamicStorageTag
        {
        };

        // Standard constructors (host-visible, dynamic)
        VulkanStorageBuffer(uint32_t size, uint32_t binding);
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding);

        // GPU-only immutable storage (device-local, no CPU access after init)
        VulkanStorageBuffer(uint32_t size, uint32_t binding, bool gpuOnly);
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, bool gpuOnly);

        // GPU dynamic storage (device-local + staging, CUDA interop compatible)
        VulkanStorageBuffer(uint32_t size, uint32_t binding, DynamicStorageTag);
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, DynamicStorageTag);

        ~VulkanStorageBuffer() override;

        void Bind(uint32_t binding) const override;
        void Unbind() const override;

        void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;
        void GetData(void* data, uint32_t size, uint32_t offset = 0) const override;

        uint32_t GetRendererID() const override { return 0; } // No GL ID in Vulkan
        uint32_t GetSize() const override { return m_Size; }

        VkBuffer             GetBuffer() const { return m_Buffer; }
        VkDescriptorBufferInfo GetDescriptorInfo() const;

    private:
        void CreateHostVisibleBuffer(uint32_t size, const void* data);
        void CreateDeviceLocalBuffer(uint32_t size, const void* data, bool withStaging);

        VkBuffer       m_Buffer        = VK_NULL_HANDLE;
        VkDeviceMemory m_BufferMemory  = VK_NULL_HANDLE;
        VkBuffer       m_StagingBuffer = VK_NULL_HANDLE; // For GPU dynamic storage
        VkDeviceMemory m_StagingMemory = VK_NULL_HANDLE;
        void*          m_MappedMemory  = nullptr;
        uint32_t       m_Size          = 0;
        uint32_t       m_Binding       = 0;
        bool           m_DeviceLocal   = false;
        bool           m_HasStaging    = false;
    };

} // namespace Engine
