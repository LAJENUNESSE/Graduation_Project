#pragma once

#include "Renderer/VertexArray.h"

namespace Engine
{

    class OpenGLVertexArray : public VertexArray
    {
    public:
        OpenGLVertexArray();
        ~OpenGLVertexArray() override;

        void Bind() const override;
        void Unbind() const override;

        void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer) override;
        void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer) override;

        const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const override
        {
            return m_VertexBuffers;
        }

        const Ref<IndexBuffer>& GetIndexBuffer() const override
        {
            return m_IndexBuffer;
        }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_VertexBufferIndex = 0;
        std::vector<Ref<VertexBuffer>> m_VertexBuffers;
        Ref<IndexBuffer> m_IndexBuffer;
    };

} // namespace Engine
