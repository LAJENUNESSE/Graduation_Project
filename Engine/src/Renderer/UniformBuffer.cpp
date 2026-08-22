#include "engpch.h"
#include "Renderer/UniformBuffer.h"

#include "Core/Assert.h"
#include "Debug/GpuMemoryStats.h"
#include "Platform/OpenGL/OpenGLUniformBuffer.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanBuffer.h"
#endif

namespace Engine
{

    Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding)
    {
        Ref<UniformBuffer> ref;
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            ref = CreateRef<OpenGLUniformBuffer>(size, binding);
            break;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            ref = CreateRef<VulkanUniformBuffer>(size, binding);
            break;
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        if (!ref)
            return nullptr;

        GpuMemoryStats::Get().TrackResource(ref, GpuMemCategory::UniformStorage, size,
                                            "UBO " + GpuMemFormatBytes(size) + " @" + std::to_string(binding));
        return ref;
    }

} // namespace Engine
