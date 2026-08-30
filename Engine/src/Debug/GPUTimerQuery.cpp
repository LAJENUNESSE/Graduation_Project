#include "engpch.h"
#include "Debug/GPUTimerQuery.h"

#include "Core/Assert.h"
#include "Renderer/RendererAPI.h"

#include "Platform/OpenGL/OpenGLGPUTimerQuery.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanGPUTimerQuery.h"
#endif

namespace Engine
{

    Ref<GPUTimerQuery> GPUTimerQuery::Create()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLGPUTimerQuery>();
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateRef<VulkanGPUTimerQuery>();
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
