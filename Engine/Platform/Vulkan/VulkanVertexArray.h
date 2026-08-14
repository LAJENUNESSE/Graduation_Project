#pragma once

#include "Renderer/VertexArray.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace Engine
{

    // Vulkan 版 VertexArray：只聚合 VertexBuffer / IndexBuffer，
    // 不做任何 GPU 端对象（Vulkan 无全局 VAO 概念）。
    // 顶点输入描述（binding / attribute）在 DrawIndexed 录制阶段由
    // BuildBindingDescriptions / BuildAttributeDescriptions 现场生成，
    // 供 graphics pipeline 创建与 vkCmdBindVertexBuffers 使用。
    class VulkanVertexArray : public VertexArray
    {
    public:
        VulkanVertexArray()           = default;
        ~VulkanVertexArray() override = default;

        void Bind() const override;
        void Unbind() const override;

        void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) override;
        void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) override;

        const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const override { return m_VertexBuffers; }
        const Ref<IndexBuffer>&               GetIndexBuffer() const override { return m_IndexBuffer; }

        // ---- 顶点输入描述生成（供 graphics pipeline / DrawIndexed 使用）----

        // 每个 vertex buffer 一个 binding，binding 号 = m_VertexBuffers 下标。
        std::vector<VkVertexInputBindingDescription> BuildBindingDescriptions() const;

        // 展开每个 buffer 的 BufferLayout 为逐个 attribute，location 全局递增。
        std::vector<VkVertexInputAttributeDescription> BuildAttributeDescriptions() const;

    private:
        std::vector<Ref<VertexBuffer>> m_VertexBuffers;
        Ref<IndexBuffer>               m_IndexBuffer;
    };

} // namespace Engine
