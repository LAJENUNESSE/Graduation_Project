#include "engpch.h"
#include "Renderer/RenderCommand.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLRendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanRendererAPI.h"
#endif

namespace Engine
{

    Scope<RendererAPI> RenderCommand::s_RendererAPI = []() -> Scope<RendererAPI>
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateScope<OpenGLRendererAPI>();
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateScope<VulkanRendererAPI>();
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in! Enable ENGINE_ENABLE_VULKAN.");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }();

} // namespace Engine
