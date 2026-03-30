#include "engpch.h"
#include "Renderer/StorageBuffer.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLStorageBuffer.h"
#include "Renderer/RendererAPI.h"

namespace Engine
{

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::Create(uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(size, binding);
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::Create(const void* data, uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(data, size, binding);
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::CreateGPUOnly(uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(size, binding, true);
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::CreateGPUOnly(const void* data, uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(data, size, binding, true);
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::CreateGPUDynamic(uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(size, binding, OpenGLStorageBuffer::DynamicStorageTag{});
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::CreateGPUDynamic(const void* data, uint32_t size, uint32_t binding)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLStorageBuffer>(data, size, binding, OpenGLStorageBuffer::DynamicStorageTag{});
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
