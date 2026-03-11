#include "engpch.h"
#include "Renderer/StorageBuffer.h"
#include "Platform/OpenGL/OpenGLStorageBuffer.h"
#include "Renderer/RendererAPI.h"

namespace Engine
{

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::Create(uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(size, binding);
        default:
            return nullptr;
        }
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::Create(const void* data, uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(data, size, binding);
        default:
            return nullptr;
        }
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::CreateGPUOnly(uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(size, binding, true);
        default:
            return nullptr;
        }
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::CreateGPUOnly(const void* data, uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(data, size, binding, true);
        default:
            return nullptr;
        }
    }

} // namespace Engine
