#include "engpch.h"
#include "Renderer/StorageBuffer.h"

#include "Core/Assert.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/OpenGL/OpenGLStorageBuffer.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanBuffer.h"
#endif

namespace Engine
{

    static std::string SsbLabel(uint32_t size, uint32_t binding)
    {
        return "SSBO " + GpuMemFormatBytes(size) + " @" + std::to_string(binding);
    }

    Ref<ShaderStorageBuffer> ShaderStorageBuffer::Create(uint32_t size, uint32_t binding, GpuMemCategory memCategory)
    {
        Ref<ShaderStorageBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLStorageBuffer>(size, binding);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanStorageBuffer>(size, binding);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        GpuMemoryStats::Get().TrackResource(ref, memCategory, size, SsbLabel(size, binding));
        return ref;
    }

    Ref<ShaderStorageBuffer>
    ShaderStorageBuffer::Create(const void* data, uint32_t size, uint32_t binding, GpuMemCategory memCategory)
    {
        Ref<ShaderStorageBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLStorageBuffer>(data, size, binding);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanStorageBuffer>(data, size, binding);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        auto& gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, memCategory, size, SsbLabel(size, binding));
        gmem.AddUploaded(size);
        return ref;
    }

    Ref<ShaderStorageBuffer>
    ShaderStorageBuffer::CreateGPUOnly(uint32_t size, uint32_t binding, GpuMemCategory memCategory)
    {
        Ref<ShaderStorageBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLStorageBuffer>(size, binding, true);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanStorageBuffer>(size, binding, true);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        GpuMemoryStats::Get().TrackResource(ref, memCategory, size, SsbLabel(size, binding));
        return ref;
    }

    Ref<ShaderStorageBuffer>
    ShaderStorageBuffer::CreateGPUOnly(const void* data, uint32_t size, uint32_t binding, GpuMemCategory memCategory)
    {
        Ref<ShaderStorageBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLStorageBuffer>(data, size, binding, true);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanStorageBuffer>(data, size, binding, true);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        auto& gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, memCategory, size, SsbLabel(size, binding));
        gmem.AddUploaded(size);
        return ref;
    }

    Ref<ShaderStorageBuffer>
    ShaderStorageBuffer::CreateGPUDynamic(uint32_t size, uint32_t binding, GpuMemCategory memCategory)
    {
        Ref<ShaderStorageBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLStorageBuffer>(size, binding, OpenGLStorageBuffer::DynamicStorageTag{});
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanStorageBuffer>(size, binding, VulkanStorageBuffer::DynamicStorageTag{});
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        GpuMemoryStats::Get().TrackResource(ref, memCategory, size, SsbLabel(size, binding));
        return ref;
    }

    Ref<ShaderStorageBuffer>
    ShaderStorageBuffer::CreateGPUDynamic(const void* data, uint32_t size, uint32_t binding, GpuMemCategory memCategory)
    {
        Ref<ShaderStorageBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLStorageBuffer>(data, size, binding, OpenGLStorageBuffer::DynamicStorageTag{});
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanStorageBuffer>(data, size, binding, VulkanStorageBuffer::DynamicStorageTag{});
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        auto& gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, memCategory, size, SsbLabel(size, binding));
        gmem.AddUploaded(size);
        return ref;
    }

} // namespace Engine
