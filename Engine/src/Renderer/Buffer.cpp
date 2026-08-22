#include "engpch.h"
#include "Renderer/Buffer.h"

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/OpenGL/OpenGLBuffer.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanBuffer.h"
#endif

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

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown ShaderDataType!");
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

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    Ref<VertexBuffer> VertexBuffer::Create(uint32_t size)
    {
        Ref<VertexBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLVertexBuffer>(size);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanVertexBuffer>(size);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        GpuMemoryStats::Get().TrackResource(ref, GpuMemCategory::MeshBuffer, size, "VBO " + GpuMemFormatBytes(size));
        return ref;
    }

    Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
    {
        Ref<VertexBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLVertexBuffer>(vertices, size);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanVertexBuffer>(vertices, size);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        auto& gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::MeshBuffer, size, "VBO " + GpuMemFormatBytes(size));
        gmem.AddUploaded(size);
        return ref;
    }

    Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        Ref<IndexBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLIndexBuffer>(indices, count);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanIndexBuffer>(indices, count);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        auto& gmem = GpuMemoryStats::Get();
        gmem.TrackResource(ref, GpuMemCategory::MeshBuffer, uint64_t(count) * 4,
                           "IBO " + std::to_string(count) + " idx", count / 3);
        gmem.AddUploaded(uint64_t(count) * 4);
        return ref;
    }

} // namespace Engine
