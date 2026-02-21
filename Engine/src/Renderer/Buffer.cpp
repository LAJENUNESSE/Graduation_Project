#include "engpch.h"
#include "Renderer/Buffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Renderer/RendererAPI.h"
#include "Platform/OpenGL/OpenGLBuffer.h"

namespace Engine
{

    uint32_t ShaderDataTypeSize(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:
            return 4;
        case ShaderDataType::Float2:
            return 4 * 2;
        case ShaderDataType::Float3:
            return 4 * 3;
        case ShaderDataType::Float4:
            return 4 * 4;
        case ShaderDataType::Int:
            return 4;
        case ShaderDataType::Int2:
            return 4 * 2;
        case ShaderDataType::Int3:
            return 4 * 3;
        case ShaderDataType::Int4:
            return 4 * 4;
        case ShaderDataType::Mat3:
            return 4 * 3 * 3;
        case ShaderDataType::Mat4:
            return 4 * 4 * 4;
        case ShaderDataType::Bool:
            return 1;
        case ShaderDataType::None:
            break;
        }

        ENGINE_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    uint32_t ShaderDataTypeComponentCount(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:
            return 1;
        case ShaderDataType::Float2:
            return 2;
        case ShaderDataType::Float3:
            return 3;
        case ShaderDataType::Float4:
            return 4;
        case ShaderDataType::Int:
            return 1;
        case ShaderDataType::Int2:
            return 2;
        case ShaderDataType::Int3:
            return 3;
        case ShaderDataType::Int4:
            return 4;
        case ShaderDataType::Mat3:
            return 3 * 3;
        case ShaderDataType::Mat4:
            return 4 * 4;
        case ShaderDataType::Bool:
            return 1;
        case ShaderDataType::None:
            break;
        }

        ENGINE_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    Ref<VertexBuffer> VertexBuffer::Create(uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLVertexBuffer>(size);
        }

        ENGINE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLVertexBuffer>(vertices, size);
        }

        ENGINE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLIndexBuffer>(indices, count);
        }

        ENGINE_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
