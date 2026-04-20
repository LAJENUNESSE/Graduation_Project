#pragma once

// Stub — Vulkan buffer implementations will be added in Phase 5.
// This file exists so that factory .cpp files compile when ENGINE_ENABLE_VULKAN is ON.

#include "Core/Base.h"
#include "Renderer/Buffer.h"
#include "Renderer/StorageBuffer.h"
#include "Renderer/UniformBuffer.h"

namespace Engine
{

    // ----- Vertex Buffer -----
    class VulkanVertexBuffer : public VertexBuffer
    {
    public:
        VulkanVertexBuffer(uint32_t size) { (void)size; }
        VulkanVertexBuffer(float* vertices, uint32_t size) { (void)vertices; (void)size; }
        ~VulkanVertexBuffer() override = default;

        void Bind() const override {}
        void Unbind() const override {}

        void                    SetData(const void* data, uint32_t size) override { (void)data; (void)size; }
        const BufferLayout&     GetLayout() const override { return m_Layout; }
        void                    SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

    private:
        BufferLayout m_Layout;
    };

    // ----- Index Buffer -----
    class VulkanIndexBuffer : public IndexBuffer
    {
    public:
        VulkanIndexBuffer(uint32_t* indices, uint32_t count) : m_Count(count) { (void)indices; }
        ~VulkanIndexBuffer() override = default;

        void     Bind() const override {}
        void     Unbind() const override {}
        uint32_t GetCount() const override { return m_Count; }

    private:
        uint32_t m_Count = 0;
    };

    // ----- Shader Storage Buffer -----
    class VulkanStorageBuffer : public ShaderStorageBuffer
    {
    public:
        struct DynamicStorageTag {};

        VulkanStorageBuffer(uint32_t size, uint32_t binding) { (void)size; (void)binding; }
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding) { (void)data; (void)size; (void)binding; }
        VulkanStorageBuffer(uint32_t size, uint32_t binding, bool gpuOnly) { (void)size; (void)binding; (void)gpuOnly; }
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, bool gpuOnly) { (void)data; (void)size; (void)binding; (void)gpuOnly; }
        VulkanStorageBuffer(uint32_t size, uint32_t binding, DynamicStorageTag) { (void)size; (void)binding; }
        VulkanStorageBuffer(const void* data, uint32_t size, uint32_t binding, DynamicStorageTag) { (void)data; (void)size; (void)binding; }
        ~VulkanStorageBuffer() override = default;

        void     Bind(uint32_t binding) const override { (void)binding; }
        void     Unbind() const override {}
        void     SetData(const void* data, uint32_t size, uint32_t offset = 0) override { (void)data; (void)size; (void)offset; }
        void     GetData(void* outData, uint32_t size, uint32_t offset = 0) const override { (void)outData; (void)size; (void)offset; }
        uint32_t GetRendererID() const override { return 0; }
        uint32_t GetSize() const override { return 0; }
        void     ClearToZero() override {}
    };

    // ----- Uniform Buffer -----
    class VulkanUniformBuffer : public UniformBuffer
    {
    public:
        VulkanUniformBuffer(uint32_t size, uint32_t binding) { (void)size; (void)binding; }
        ~VulkanUniformBuffer() override = default;

        void SetData(const void* data, uint32_t size, uint32_t offset = 0) override
        {
            (void)data; (void)size; (void)offset;
        }
    };

} // namespace Engine
