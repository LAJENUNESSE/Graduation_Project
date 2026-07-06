#include "engpch.h"
#include "Renderer/RenderCommand.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLRendererAPI.h"

#ifdef ENGINE_ENABLE_VULKAN
#include "Platform/Vulkan/VulkanRendererAPI.h"
#endif

namespace Engine
{

    Scope<RendererAPI> RenderCommand::s_RendererAPI = nullptr;

    void RenderCommand::CreateRendererAPI()
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return;
        case RendererAPI::API::OpenGL:
            s_RendererAPI = CreateScope<OpenGLRendererAPI>();
            return;
        case RendererAPI::API::Vulkan:
#ifdef ENGINE_ENABLE_VULKAN
            s_RendererAPI = CreateScope<VulkanRendererAPI>();
#else
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not compiled in! Enable ENGINE_ENABLE_VULKAN.");
#endif
            return;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
    }

    void RenderCommand::Init()
    {
        if (!s_RendererAPI)
            CreateRendererAPI();
        s_RendererAPI->Init();
    }

} // namespace Engine
