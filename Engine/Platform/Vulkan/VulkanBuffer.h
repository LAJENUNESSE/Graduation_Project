#pragma once

#include "Core/Base.h"
#include "Renderer/Buffer.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/UniformBuffer.h"

#include <vulkan/vulkan.h>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace Engine
{
    class VulkanVertexBuffer : public VertexBuffer
    {
    public:
        explicit VulkanVertexBuffer(uint32_t size);
        VulkanVertexBuffer(float* vertices, uint32_t size);
        ~VulkanVertexBuffer() override;

        void Bind() const override;
        void Unbind() const override;

        void                SetData(const void* data, uint32_t size) override;
        const BufferLayout& GetLayout() const override { return m_Layout; }
        void                SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        void Create(uint32_t size, const void* initialData);

    private:
        VkBuffer      m_Buffer     = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = nullptr;
        uint32_t      m_Size       = 0;
        BufferLayout  m_Layout;
    };

    class VulkanIndexBuffer : public IndexBuffer
    {
    public:
        VulkanIndexBuffer(uint32_t* indices, uint32_t count);
        ~VulkanIndexBuffer() override;

        void     Bind() const override;
        void     Unbind() const override;
        uint32_t GetCount() const override { return m_Count; }

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        VkBuffer      m_Buffer     = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = nullptr;
        uint32_t      m_Size       = 0;
        uint32_t      m_Count      = 0;
    };

    class VulkanStorageBuffer : public ShaderStorageBuffer
    {
    public:
        struct DynamicStorageTag
        {
        };

        VulkanStorageBuffer(uint32_t size, uint32_t binding);
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding);
        VulkanStorageBuffer(uint32_t size, uint32_t binding, bool gpuOnly);
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, bool gpuOnly);
        VulkanStorageBuffer(uint32_t size, uint32_t binding, DynamicStorageTag);
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, DynamicStorageTag);
        ~VulkanStorageBuffer() override;

        void     Bind(uint32_t binding) const override;
        void     Unbind() const override;
        void     SetData(const void* data, uint32_t size, uint32_t offset = 0) override;
        void     GetData(void* outData, uint32_t size, uint32_t offset = 0) const override;
        uint32_t GetRendererID() const override { return 0; }
        uint32_t GetSize() const override { return m_Size; }
        void     ClearToZero() override;

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        void Create(uint32_t size, uint32_t binding, const void* initialData, bool gpuOnly, bool dynamicStorage);

    private:
        VkBuffer         m_Buffer      = VK_NULL_HANDLE;
        VmaAllocation    m_Allocation  = nullptr;
        uint32_t         m_Size        = 0;
        uint32_t         m_Binding     = 0;
        mutable uint32_t m_LastBinding = 0;
        bool             m_GPUOnly     = false;
        bool             m_Dynamic     = false;
    };

    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer(uint32_t size, uint32_t binding)
        {
            (void)size;
            (void)binding;
        }
        ~VulkanUniformBuffer() override = default;

        void SetData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            (void)data;
            (void)size;
            (void)offset;
        }
    };

} // namespace Engine
