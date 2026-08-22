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

        // Phase 7.5 占位：未来 CUDA-Vulkan 互操作时 buffer 需用 VK_EXTERNAL_MEMORY_HANDLE_TYPE_*
        // 创建以导出 Win32/Fd handle 给 CUDA 导入。Phase 7 默认 None；上层透传该枚举即可。
        enum class ExternalMemoryHint : uint8_t
        {
            None        = 0,
            CudaInterop = 1, // 启用后断言 Phase 7.5 未实现
        };

        VulkanStorageBuffer(uint32_t size, uint32_t binding, ExternalMemoryHint hint = ExternalMemoryHint::None);
        VulkanStorageBuffer(const void*        data,
                            uint32_t           size,
                            uint32_t           binding,
                            ExternalMemoryHint hint = ExternalMemoryHint::None);
        VulkanStorageBuffer(uint32_t           size,
                            uint32_t           binding,
                            bool               gpuOnly,
                            ExternalMemoryHint hint = ExternalMemoryHint::None);
        VulkanStorageBuffer(const void*        data,
                            uint32_t           size,
                            uint32_t           binding,
                            bool               gpuOnly,
                            ExternalMemoryHint hint = ExternalMemoryHint::None);
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
        void Create(uint32_t           size,
                    uint32_t           binding,
                    const void*        initialData,
                    bool               gpuOnly,
                    bool               dynamicStorage,
                    ExternalMemoryHint hint = ExternalMemoryHint::None);

    private:
        VkBuffer         m_Buffer      = VK_NULL_HANDLE;
        VmaAllocation    m_Allocation  = nullptr;
        uint32_t         m_Size        = 0;
        uint32_t         m_Binding     = 0;
        mutable uint32_t m_LastBinding = 0;
        bool             m_GPUOnly     = false;
        bool             m_Dynamic     = false;
        bool             m_DeviceLocal = false; // GPUOnly/GPUDynamic → device-local + staging 数据通路
    };

    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer(uint32_t size, uint32_t binding);
        ~VulkanUniformBuffer() override;

        void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        VkBuffer      m_Buffer     = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = nullptr;
        uint32_t      m_Size       = 0;
        uint32_t      m_Binding    = 0;
    };

} // namespace Engine
