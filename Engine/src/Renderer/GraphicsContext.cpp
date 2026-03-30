#include "engpch.h"
#include "Renderer/GraphicsContext.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Renderer/RendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanContext.h"
#endif

namespace Engine
{

    Scope<GraphicsContext> GraphicsContext::Create(void* window)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            return CreateScope<VulkanContext>(static_cast<GLFWwindow*>(window));
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not enabled in build!");
            return nullptr;
#endif
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
