#include "engpch.h"
#include "Renderer/GPUAsyncReadback.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLAsyncReadback.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanAsyncReadback.h"
#endif

namespace Engine
{

    Ref<GPUAsyncReadback> GPUAsyncReadback::Create(uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLAsyncReadback>(size);
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanAsyncReadback>(size);
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "Vulkan backend not enabled in this build");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
