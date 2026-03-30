#include "engpch.h"
#include "Renderer/Framebuffer.h"

#include "Core/Assert.h"
#include "Platform/OpenGL/OpenGLFramebuffer.h"
#include "Renderer/RendererAPI.h"

namespace Engine
{

    Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::None is currently not supported!");
            return nullptr;
        case RendererAPI::API::OpenGL:
            return CreateRef<OpenGLFramebuffer>(spec);
        case RendererAPI::API::Vulkan:
            ENGINE_CORE_RELEASE_ASSERT(false, "RendererAPI::Vulkan is not yet implemented!");
            return nullptr;
        }

        ENGINE_CORE_RELEASE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

} // namespace Engine
