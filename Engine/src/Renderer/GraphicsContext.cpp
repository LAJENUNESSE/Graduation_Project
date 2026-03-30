#include "engpch.h"
#include "Renderer/GraphicsContext.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Renderer/RendererAPI.h"

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
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
