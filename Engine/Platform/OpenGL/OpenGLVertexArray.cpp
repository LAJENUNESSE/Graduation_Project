#include "engpch.h"
#include "Platform/OpenGL/OpenGLVertexArray.h"

#include <glad/gl.h>

#include "Core/Assert.h"
#include "Core/Log.h"

namespace Engine
{

    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:
        case ShaderDataType::Float2:
        case ShaderDataType::Float3:
        case ShaderDataType::Float4:
        case ShaderDataType::Mat3:
        case ShaderDataType::Mat4:
            return GL_FLOAT;
        case ShaderDataType::Int:
        case ShaderDataType::Int2:
        case ShaderDataType::Int3:
        case ShaderDataType::Int4:
            return GL_INT;
        case ShaderDataType::Bool:
            return GL_BOOL;
        case ShaderDataType::None:
            break;
        }

        ENGINE_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    OpenGLVertexArray::OpenGLVertexArray()
    {
        glGenVertexArrays(1, &m_RendererID);
    }

    OpenGLVertexArray::~OpenGLVertexArray()
    {
        glDeleteVertexArrays(1, &m_RendererID);
    }

    void OpenGLVertexArray::Bind() const
    {
        glBindVertexArray(m_RendererID);
    }

    void OpenGLVertexArray::Unbind() const
    {
        glBindVertexArray(0);
    }

    void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
    {
        ENGINE_CORE_ASSERT(vertexBuffer->GetLayout().GetElements().size(), "Vertex buffer has no layout!");

        glBindVertexArray(m_RendererID);
        vertexBuffer->Bind();

        const auto& layout = vertexBuffer->GetLayout();
        for (const auto& element : layout)
        {
            switch (element.Type)
            {
            case ShaderDataType::Float:
            case ShaderDataType::Float2:
            case ShaderDataType::Float3:
            case ShaderDataType::Float4:
            {
                glEnableVertexAttribArray(m_VertexBufferIndex);
                glVertexAttribPointer(m_VertexBufferIndex, ShaderDataTypeComponentCount(element.Type),
                                      ShaderDataTypeToOpenGLBaseType(element.Type),
                                      element.Normalized ? GL_TRUE : GL_FALSE, layout.GetStride(),
                                      reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset)));
                m_VertexBufferIndex++;
                break;
            }
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4:
            case ShaderDataType::Bool:
            {
                glEnableVertexAttribArray(m_VertexBufferIndex);
                glVertexAttribIPointer(m_VertexBufferIndex, ShaderDataTypeComponentCount(element.Type),
                                       ShaderDataTypeToOpenGLBaseType(element.Type), layout.GetStride(),
                                       reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset)));
                m_VertexBufferIndex++;
                break;
            }
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4:
            {
                uint32_t count = (element.Type == ShaderDataType::Mat3) ? 3 : 4;
                for (uint32_t i = 0; i < count; i++)
                {
                    glEnableVertexAttribArray(m_VertexBufferIndex);
                    glVertexAttribPointer(m_VertexBufferIndex, count, GL_FLOAT,
                                          element.Normalized ? GL_TRUE : GL_FALSE, layout.GetStride(),
                                          reinterpret_cast<const void*>(
                                              static_cast<uintptr_t>(element.Offset + sizeof(float) * count * i)));
                    glVertexAttribDivisor(m_VertexBufferIndex, 1);
                    m_VertexBufferIndex++;
                }
                break;
            }
            default:
                ENGINE_CORE_ASSERT(false, "Unknown ShaderDataType!");
            }
        }

        m_VertexBuffers.push_back(vertexBuffer);
    }

    void OpenGLVertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
    {
        glBindVertexArray(m_RendererID);
        indexBuffer->Bind();

        m_IndexBuffer = indexBuffer;
    }

} // namespace Engine
